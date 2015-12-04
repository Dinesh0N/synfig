/* === S Y N F I G ========================================================= */
/*!	\file synfig/rendering/renderer.cpp
**	\brief Renderer
**
**	$Id$
**
**	\legal
**	......... ... 2015 Ivan Mahonin
**
**	This package is free software; you can redistribute it and/or
**	modify it under the terms of the GNU General Public License as
**	published by the Free Software Foundation; either version 2 of
**	the License, or (at your option) any later version.
**
**	This package is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**	General Public License for more details.
**	\endlegal
*/
/* ========================================================================= */

/* === H E A D E R S ======================================================= */

#ifdef USING_PCH
#	include "pch.h"
#else
#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#ifndef WIN32
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#endif

#include <cstdlib>
#include <climits>

#include <typeinfo>

#include <glibmm/threads.h>

#include <synfig/general.h>
#include <synfig/localization.h>
#include <synfig/debug/debugsurface.h>
#include <synfig/debug/log.h>
#include <synfig/debug/measure.h>

#include "renderer.h"

#include "software/renderersw.h"
#include "software/renderersafe.h"
#include "opengl/renderergl.h"
#include "common/task/taskcallback.h"
#include "opengl/task/taskgl.h"

#endif

using namespace synfig;
using namespace rendering;


#define SYNFIG_RENDERING_MAX_THREADS 256


#ifdef _DEBUG
//#define DEBUG_TASK_LIST
#define DEBUG_TASK_MEASURE
//#define DEBUG_TASK_SURFACE
//#define DEBUG_OPTIMIZATION
//#define DEBUG_OPTIMIZATION_EACH_CHANGE
//#define DEBUG_OPTIMIZATION_MEASURE
//#define DEBUG_OPTIMIZATION_COUNTERS
//#define DEBUG_THREAD_TASK
//#define DEBUG_THREAD_WAIT
#endif


/* === M A C R O S ========================================================= */

/* === G L O B A L S ======================================================= */

/* === P R O C E D U R E S ================================================= */

/* === M E T H O D S ======================================================= */

Renderer::Handle Renderer::blank;
std::map<String, Renderer::Handle> *Renderer::renderers;
Renderer::Queue *Renderer::queue;
Renderer::DebugOptions Renderer::debug_options;

class Renderer::Queue
{
public:
	typedef std::list<Glib::Threads::Thread*> ThreadList;
	typedef std::map<int, Task::Handle> ThreadTaskMap;
	typedef std::set<Task::Handle> TaskSet;
	typedef std::list<Task::Handle> TaskQueue;

private:
	Glib::Threads::Mutex mutex;
	Glib::Threads::Mutex threads_mutex;
	Glib::Threads::Cond cond;
	Glib::Threads::Cond condgl;

	TaskQueue ready_tasks;
	TaskQueue gl_ready_tasks;
	TaskSet not_ready_tasks;
	TaskSet gl_not_ready_tasks;

	bool started;

	ThreadList threads;
	ThreadTaskMap tasks_in_process;

	void start() {
		Glib::Threads::Mutex::Lock lock(mutex);
		if (started) return;

		// one thread reserved for OpenGL
		// also this thread almost don't use CPU time
		// so we have ~50% of one core for GUI
		int count = g_get_num_processors();

		#ifdef DEBUG_TASK_SURFACE
		count = 2;
		#endif

		if (const char *s = getenv("SYNFIG_RENDERING_THREADS"))
			count = atoi(s) + 1;

		if (count > SYNFIG_RENDERING_MAX_THREADS) count = SYNFIG_RENDERING_MAX_THREADS;
		if (count < 2) count = 2;

		for(int i = 0; i < count; ++i)
			threads.push_back(
				Glib::Threads::Thread::create(
					sigc::bind(sigc::mem_fun(*this, &Queue::process), i) ));
		info("rendering threads %d", count);
		started = true;
	}

	void stop() {
		{
			Glib::Threads::Mutex::Lock lock(mutex);
			started = false;
			cond.broadcast();
			condgl.broadcast();
		}
		while(!threads.empty())
			{ threads.front()->join(); threads.pop_front(); }
	}

	void process(int thread_index)
	{
		while(Task::Handle task = get(thread_index))
		{
			#ifdef DEBUG_THREAD_TASK
			info("thread %d: begin task #%d '%s'", thread_index, task->index, typeid(*task).name());
			#endif

			assert( task->check() );

			if (!task->run(task->params))
				task->success = false;

			#ifdef DEBUG_TASK_SURFACE
			debug::DebugSurface::save_to_file(task->target_surface, etl::strprintf("task%d", task->index));
			#endif

			#ifdef DEBUG_THREAD_TASK
			info("thread %d: end task #%d '%s'", thread_index, task->index, typeid(*task).name());
			#endif

			done(thread_index, task);
		}
	}

	void done(int thread_index, const Task::Handle &task)
	{
		assert(task);
		bool found = false;
		Glib::Threads::Mutex::Lock lock(mutex);
		for(Task::Set::iterator i = task->back_deps.begin(); i != task->back_deps.end(); ++i)
		{
			assert(*i);
			--(*i)->deps_count;
			if ((*i)->deps_count == 0)
			{
				bool gl = i->type_is<TaskGL>();
				TaskQueue &queue = gl ? gl_ready_tasks     : ready_tasks;
				TaskSet   &wait  = gl ? gl_not_ready_tasks : not_ready_tasks;
				wait.erase(*i);
				queue.push_back(*i);

				// current process will take one task,
				// so we don't need to call signal by first time
				if (!found)
					found = true;
				else
					(gl ? condgl : cond).signal();
			}
		}
		task->back_deps.clear();
		assert( tasks_in_process.count(thread_index) == 1 );
		tasks_in_process.erase(thread_index);
		//info("rendering threads used %d", tasks_in_process.size());
	}

	Task::Handle get(int thread_index) {
		Glib::Threads::Mutex::Lock lock(mutex);

		TaskQueue &queue = thread_index == 0 ? gl_ready_tasks     : ready_tasks;
		TaskSet   &wait  = thread_index == 0 ? gl_not_ready_tasks : not_ready_tasks;
		while(started)
		{
			if (!queue.empty())
			{
				Task::Handle task = queue.front();
				queue.pop_front();
				assert( tasks_in_process.count(thread_index) == 0 );
				tasks_in_process[thread_index] = task;
				//info("rendering threads used %d", tasks_in_process.size());
				return task;
			}

			#ifdef DEBUG_THREAD_WAIT
			if (!wait.empty())
				info("thread %d: rendering wait for task", thread_index);
			#endif

			assert( wait.empty() || !tasks_in_process.empty() );

			(thread_index ? cond : condgl).wait(mutex);
		}
		return Task::Handle();
	}

	static void fix_task(const Task &task, const Task::RunParams &params)
	{
		//for(Task::List::iterator i = task.back_deps.begin(); i != task.back_deps.end();)
		//	if (*i) ++i; else i = (*i)->back_deps.erase(i);
		task.params = params;
		task.success = true;
	}

public:
	Queue(): started(false) { start(); }
	~Queue() { stop(); }

	int get_threads_count() const
	{
		return threads.size();
	}

	void enqueue(const Task::Handle &task, const Task::RunParams &params)
	{
		if (!task) return;
		fix_task(*task, params);
		Glib::Threads::Mutex::Lock lock(mutex);

		bool gl = task.type_is<TaskGL>();
		TaskQueue &queue = gl ? gl_ready_tasks     : ready_tasks;
		TaskSet   &wait  = gl ? gl_not_ready_tasks : not_ready_tasks;
		if (task->deps_count == 0) {
			queue.push_back(task);
			(gl ? condgl : cond).signal();
		}
		else
		{
			wait.insert(task);
		}
	}

	void enqueue(const Task::List &tasks, const Task::RunParams &params)
	{
		int count = 0;
		for(Task::List::const_iterator i = tasks.begin(); i != tasks.end(); ++i)
			if (*i) { fix_task(**i, params); ++count; }
		if (!count) return;

		Glib::Threads::Mutex::Lock lock(mutex);
		int glsignals = 0;
		int signals = 0;
		int threads = get_threads_count() - 1;
		for(Task::List::const_iterator i = tasks.begin(); i != tasks.end(); ++i)
		{
			if (*i)
			{
				bool gl = i->type_is<TaskGL>();
				TaskQueue &queue = gl ? gl_ready_tasks     : ready_tasks;
				TaskSet   &wait  = gl ? gl_not_ready_tasks : not_ready_tasks;
				if ((*i)->deps_count == 0) {
					queue.push_back(*i);
					if (gl)
					{
						if (glsignals < 1) { condgl.signal(); ++glsignals; }
					}
					else
					{
						if (signals < threads) { cond.signal(); ++signals; }
					}
				}
				else
				{
					wait.insert(*i);
				}
			}
		}
	}

	void clear()
	{
		Glib::Threads::Mutex::Lock lock(mutex);
		ready_tasks.clear();
		gl_ready_tasks.clear();
		not_ready_tasks.clear();
		gl_not_ready_tasks.clear();
	}
};



void
Renderer::initialize_renderers()
{
	// initialize renderers
	RendererSW::initialize();
	RendererGL::initialize();

	// register renderers
	register_renderer("software", new RendererSW());
	register_renderer("gl", new RendererGL());
	register_renderer("safe", new RendererSafe());
}

void
Renderer::deinitialize_renderers()
{
	RendererGL::deinitialize();
	RendererSW::deinitialize();
}



Renderer::~Renderer() { }

int
Renderer::get_max_simultaneous_threads() const
{
	assert(queue);
	return queue->get_threads_count() - 1;
}

bool
Renderer::is_optimizer_registered(const Optimizer::Handle &optimizer) const
{
	if (!optimizer)
		for(Optimizer::List::const_iterator i = optimizers[optimizer->category_id].begin(); i != optimizers[optimizer->category_id].end(); ++i)
			if (*i == optimizer) return true;
	return false;
}

void
Renderer::register_optimizer(const Optimizer::Handle &optimizer)
{
	if (optimizer) {
		assert(!is_optimizer_registered(optimizer));
		optimizers[optimizer->category_id].push_back(optimizer);
	}
}

void
Renderer::unregister_optimizer(const Optimizer::Handle &optimizer)
{
	for(Optimizer::List::iterator i = optimizers[optimizer->category_id].begin(); i != optimizers[optimizer->category_id].end();)
		if (*i == optimizer) i = optimizers[optimizer->category_id].erase(i); else ++i;
}

void
Renderer::optimize_recursive(
	const Optimizer::List &optimizers,
	const Optimizer::RunParams& params,
	int &calls_count,
	int &optimizations_count,
	int max_level ) const
{
	if (!params.ref_task) return;

	// check dependencies
	if (params.ref_affects_to & params.depends_from) return;

	// run all non-deep-first optimizers for current task
	// before processing of sub-tasks (see Optimizer::deep_first)
	for(Optimizer::List::const_iterator i = optimizers.begin(); i != optimizers.end(); ++i)
	{
		if (!(*i)->deep_first)
		{
			if ((*i)->for_task || ((*i)->for_root_task && !params.parent))
			{
				// run
				Optimizer::RunParams p(params);
				(*i)->run(p);

				++calls_count;
				if (params.ref_task != p.ref_task)
				{
					++optimizations_count;
					#ifdef DEBUG_OPTIMIZATION_EACH_CHANGE
					log("", params.list, (typeid(**i).name() + 19), &p);
					#endif
				}

				// apply params
				params.ref_affects_to |= p.ref_affects_to;
				params.ref_mode |= p.ref_mode;
				params.ref_task = p.ref_task;

				// return when current task removed
				if (!params.ref_task) return;

				assert( params.ref_task->check() );

				// check dependencies
				if (params.ref_affects_to & params.depends_from) return;
			}
		}
	}

	// process sub-tasks, only for non-for-root-task optimizers (see Optimizer::for_root_task)
	if (max_level > 0)
	{
		bool task_clonned = false;
		bool nonrecursive = false;
		bool recursive = false;
		Optimizer::RunParams initial_params(params);
		for(Task::List::iterator i = params.ref_task->sub_tasks.begin(); i != params.ref_task->sub_tasks.end();)
		{
			if (*i)
			{
				// recursive run
				initial_params.ref_task = params.ref_task;
				Optimizer::RunParams sub_params = initial_params.sub(*i);
				optimize_recursive(optimizers, sub_params, calls_count, optimizations_count, nonrecursive ? 1 : recursive ? INT_MAX : max_level-1);
				nonrecursive = false;
				recursive = false;

				// replace sub-task if optimized
				if (sub_params.ref_task != *i)
				{
					// before replacement clone current task (if it not already cloned)
					if (!task_clonned)
					{
						int index = i - params.ref_task->sub_tasks.begin();
						params.ref_task = params.ref_task->clone();
						i = params.ref_task->sub_tasks.begin() + index; // validate iterator
						task_clonned = true;
					}
					*i = sub_params.ref_task;

					// go to next sub-task if we don't need to repeat optimization (see Optimizer::MODE_REPEAT)
					if ((sub_params.ref_mode & Optimizer::MODE_REPEAT_LAST) == Optimizer::MODE_REPEAT_LAST)
					{
						// check recursive flag (see Optimizer::MODE_RECURSIVE)
						if (sub_params.ref_mode & Optimizer::MODE_RECURSIVE)
							recursive = true;
						else
							nonrecursive = true;
					}
					else
					{
						++i;
					}
				}
				else
				{
					// go to next sub-task, when sub-task not unchanged
					++i;
				}

				// apply affected categories
				params.ref_affects_to |= sub_params.ref_affects_to;

				// if mode is REPEAT_BRUNCH then provide this flag to result
				if ((sub_params.ref_mode & Optimizer::MODE_REPEAT_BRUNCH) == Optimizer::MODE_REPEAT_BRUNCH)
				{
					params.ref_mode |= Optimizer::MODE_REPEAT_BRUNCH;
					params.ref_mode |= (sub_params.ref_mode & Optimizer::MODE_RECURSIVE);
				}
				else
				// if mode is REPEAT_PARENT then provide flag REPEAT_LAST to result (repeat only one upper level)
				if ((sub_params.ref_mode & Optimizer::MODE_REPEAT_PARENT) == Optimizer::MODE_REPEAT_PARENT)
				{
					params.ref_mode |= Optimizer::MODE_REPEAT_LAST;
					params.ref_mode |= (sub_params.ref_mode & Optimizer::MODE_RECURSIVE);
				}

				// check dependencies
				if (params.ref_affects_to & params.depends_from) return;
			}
			else
			{
				// skip nulls
				++i;
			}
		}
	}

	// run deep-first optimizers for current task
	// when all sub-tasks are processed (see Optimizer::deep_first)
	for(Optimizer::List::const_iterator i = optimizers.begin(); i != optimizers.end(); ++i)
	{
		if ((*i)->deep_first)
		{
			if ((*i)->for_task || ((*i)->for_root_task && !params.parent))
			{
				// run
				Optimizer::RunParams p(params);
				(*i)->run(p);

				++calls_count;
				if (params.ref_task != p.ref_task)
				{
					++optimizations_count;
					#ifdef DEBUG_OPTIMIZATION_EACH_CHANGE
					log("", params.list, (typeid(**i).name() + 19), &p);
					#endif
				}

				// apply params
				params.ref_affects_to |= p.ref_affects_to;
				params.ref_mode |= p.ref_mode;
				params.ref_task = p.ref_task;

				// return when current task removed
				if (!params.ref_task) return;

				assert( params.ref_task->check() );

				// check dependencies
				if (params.ref_affects_to & params.depends_from) return;
			}
		}
	}

}

void
Renderer::optimize(Task::List &list) const
{
	//debug::Measure t("Renderer::optimize");

	int current_category_id = 0;
	int current_optimizer_index = 0;
	Optimizer::Category current_affected = 0;
	Optimizer::Category categories_to_process = Optimizer::CATEGORY_ALL;
	Optimizer::List single(1);

	while(categories_to_process &= Optimizer::CATEGORY_ALL)
	{
		if (current_category_id >= Optimizer::CATEGORY_ID_COUNT)
		{
			current_category_id = 0;
			current_optimizer_index = 0;
			current_affected = 0;
			continue;
		}

		if (!((1 << current_category_id) & categories_to_process))
		{
			++current_category_id;
			current_optimizer_index = 0;
			current_affected = 0;
			continue;
		}

		if (current_optimizer_index >= (int)optimizers[current_category_id].size())
		{
			categories_to_process &= ~(1 << current_category_id);
			categories_to_process |= current_affected;
			++current_category_id;
			current_optimizer_index = 0;
			current_affected = 0;
			continue;
		}

		bool simultaneous_run = Optimizer::categories_info[current_category_id].simultaneous_run;
		const Optimizer::List &current_optimizers = simultaneous_run ? optimizers[current_category_id] : single;
		if (!simultaneous_run) {
			single.front() = optimizers[current_category_id][current_optimizer_index];
			Optimizer::Category depends_from_self = (1 << current_category_id) & single.front()->depends_from;
			if (current_affected & depends_from_self)
			{
				current_category_id = 0;
				current_optimizer_index = 0;
				current_affected = 0;
				continue;
			}
		}

		Optimizer::Category depends_from = 0;
		bool for_list = false;
		bool for_task = false;
		bool for_root_task = false;
		for(Optimizer::List::const_iterator i = current_optimizers.begin(); i != current_optimizers.end(); ++i)
		{
			depends_from |= ((1 << current_category_id) - 1) & (*i)->depends_from;
			if ((*i)->for_list) for_list = true;
			if ((*i)->for_task) for_task = true;
			if ((*i)->for_root_task) for_root_task = true;
		}

		#ifdef DEBUG_OPTIMIZATION
		log("", list, etl::strprintf("before optimize category %d index %d", current_category_id, current_optimizer_index));
		#endif

		#ifdef DEBUG_OPTIMIZATION_MEASURE
		debug::Measure t(etl::strprintf("optimize category %d index %d", current_category_id, current_optimizer_index));
		#endif

		if (for_list)
		{
			for(Optimizer::List::const_iterator i = current_optimizers.begin(); !(categories_to_process & depends_from) && i != current_optimizers.end(); ++i)
			{
				if ((*i)->for_list)
				{
					Optimizer::RunParams params(*this, list, depends_from);
					(*i)->run(params);
					categories_to_process |= current_affected |= params.ref_affects_to;
				}
			}
		}

		if (for_task || for_root_task)
		{
			int calls_count = 0;
			int optimizations_count = 0;

			bool nonrecursive = false;
			for(Task::List::iterator j = list.begin(); !(categories_to_process & depends_from) && j != list.end();)
			{
				if (*j)
				{
					Optimizer::RunParams params(*this, list, depends_from, *j);
					optimize_recursive(current_optimizers, params, calls_count, optimizations_count, !for_task ? 0 : nonrecursive ? 1 : INT_MAX);
					nonrecursive = false;

					if (*j != params.ref_task)
					{

						if (params.ref_task)
						{
							*j = params.ref_task;
							// go to next sub-task if we don't need to repeat optimization (see Optimizer::MODE_REPEAT)
							if ((params.ref_mode & Optimizer::MODE_REPEAT_LAST) == Optimizer::MODE_REPEAT_LAST)
							{
								// check non-recursive flag (see Optimizer::MODE_RECURSIVE)
								if (!(params.ref_mode & Optimizer::MODE_RECURSIVE))
									nonrecursive = true;
							}
							else ++j;
						}
						else
							j = list.erase(j);
					} else ++j;
					categories_to_process |= current_affected |= params.ref_affects_to;
				}
				else
				{
					j = list.erase(j);
				}
			}

			#ifdef DEBUG_OPTIMIZATION_COUNTERS
			debug::Log::info("", "optimize category %d index %d: calls %d, changes %d",
				current_category_id, current_optimizer_index, calls_count, optimizations_count );
			#endif
		}

		if (categories_to_process & depends_from)
		{
			current_category_id = 0;
			current_optimizer_index = 0;
			current_affected = 0;
			continue;
		}

		current_optimizer_index += current_optimizers.size();
	}

	// remove nulls
	for(Task::List::iterator j = list.begin(); j != list.end();)
		if (*j) ++j; else j = list.erase(j);
}

bool
Renderer::run(const Task::List &list) const
{
	//info("renderer: %s", get_name().c_str());
	//info("renderer.debug.task_list_log: %s", get_debug_options().task_list_log.c_str());
	//info("renderer.debug.task_list_optimized_log: %s", get_debug_options().task_list_optimized_log.c_str());
	//info("renderer.debug.result_image: %s", get_debug_options().result_image.c_str());

	#ifndef NDEBUG
	for(Task::List::const_iterator j = list.begin(); j != list.end(); ++j)
		assert( (*j)->check() );
	#endif

	#ifdef DEBUG_TASK_MEASURE
	debug::Measure t("Renderer::run");
	#endif

	#ifdef DEBUG_TASK_LIST
	log("", list, "input list");
	#endif

	if (!get_debug_options().task_list_log.empty())
		log(get_debug_options().task_list_log, list, "input list");

	Task::List optimized_list(list);
	{
		#ifdef DEBUG_TASK_MEASURE
		debug::Measure t("optimize");
		#endif
		optimize(optimized_list);
	}

	{
		#ifdef DEBUG_TASK_MEASURE
		debug::Measure t("find deps");
		#endif

		for(Task::List::const_iterator i = optimized_list.begin(); i != optimized_list.end(); ++i)
		{
			(*i)->back_deps.clear();
			(*i)->deps_count = 0;
		}

		for(Task::List::const_iterator i = optimized_list.begin(); i != optimized_list.end(); ++i)
		{
			assert((*i)->index == 0);
			(*i)->index = i - optimized_list.begin() + 1;
			if ((*i)->valid_target())
			{
				for(Task::List::const_iterator j = (*i)->sub_tasks.begin(); j != (*i)->sub_tasks.end(); ++j)
					if (*j && (*j)->valid_target())
						for(Task::List::const_reverse_iterator rk(i); rk != optimized_list.rend(); ++rk)
							if ( (*j)->target_surface == (*rk)->target_surface
							  && (*rk)->valid_target()
							  && etl::intersect((*j)->get_target_rect(), (*rk)->get_target_rect()) )
								if ((*rk)->back_deps.insert(*i).second)
									++(*i)->deps_count;
				for(Task::List::const_reverse_iterator rk(i); rk != optimized_list.rend(); ++rk)
					if ( (*i)->target_surface == (*rk)->target_surface
					  && (*rk)->valid_target()
					  && etl::intersect((*i)->get_target_rect(), (*rk)->get_target_rect()) )
						if ((*rk)->back_deps.insert(*i).second)
							++(*i)->deps_count;
			}
		}
	}

	#ifdef DEBUG_TASK_LIST
	log("", optimized_list, "optimized list");
	#endif

	if (!get_debug_options().task_list_optimized_log.empty())
		log(get_debug_options().task_list_optimized_log, optimized_list, "optimized list");

	bool success = true;

	{
		#ifdef DEBUG_TASK_MEASURE
		debug::Measure t("run tasks");
		#endif

		Glib::Threads::Cond cond;
		Glib::Threads::Mutex mutex;
		Glib::Threads::Mutex::Lock lock(mutex);

		TaskCallbackCond::Handle task_cond = new TaskCallbackCond();
		task_cond->cond = &cond;
		task_cond->mutex = &mutex;
		for(Task::List::const_iterator i = optimized_list.begin(); i != optimized_list.end(); ++i)
			if ((*i)->back_deps.insert(task_cond).second)
				++task_cond->deps_count;
		optimized_list.push_back(task_cond);

		queue->enqueue(optimized_list, Task::RunParams());

		task_cond->cond->wait(mutex);
		if (!task_cond->success) success = false;

		if (!get_debug_options().result_image.empty())
			debug::DebugSurface::save_to_file(
				optimized_list.size() > 1
					? (*(optimized_list.rbegin()+1))->target_surface
					: Surface::Handle(),
				get_debug_options().result_image,
				true );
	}

	return success;
}

void
Renderer::log(
	const String &logfile,
	const Task::Handle &task,
	const Optimizer::RunParams* optimization_stack,
	int level ) const
{
	bool use_stack = false;
	const Optimizer::RunParams* p = optimization_stack ? optimization_stack->get_level(level) : NULL;
	Task::Handle t = task;
	if (p && p->orig_task == t)
		{ use_stack = true; t = p->ref_task; }

	if (t)
	{
		String back_deps;
		if (!t->back_deps.empty())
		{
			std::multiset<int> back_deps_set;
			for(Task::Set::const_iterator i = t->back_deps.begin(); i != t->back_deps.end(); ++i)
				back_deps_set.insert((*i)->index);
			for(std::multiset<int>::const_iterator i = back_deps_set.begin(); i != back_deps_set.end(); ++i)
				back_deps += etl::strprintf("%d ", *i);
			back_deps = "(" + back_deps.substr(0, back_deps.size()-1) + ") ";
		}

		debug::Log::info(logfile,
		      String(level*2, ' ')
			+ (use_stack ? "*" : "")
			+ (t->index ? etl::strprintf("#%d ", t->index): "")
			+ ( t->deps_count
			  ? etl::strprintf("%d ", t->deps_count )
			  : "" )
			+ back_deps
			+ (typeid(*t).name() + 19)
			+ ( t->get_bounds().valid()
			  ? etl::strprintf(" bounds (%f, %f)-(%f, %f)",
				t->get_bounds().minx, t->get_bounds().miny,
				t->get_bounds().maxx, t->get_bounds().maxy )
			  : "" )
			+ ( t->valid_target()
              ? etl::strprintf(" source (%f, %f)-(%f, %f) target (%d, %d)-(%d, %d) surface %s (%dx%d) id %d",
				t->get_source_rect_lt()[0], t->get_source_rect_lt()[1],
				t->get_source_rect_rb()[0], t->get_source_rect_rb()[1],
				t->get_target_rect().minx, t->get_target_rect().miny,
				t->get_target_rect().maxx, t->get_target_rect().maxy,
				(typeid(*t->target_surface).name() + 19),
				t->target_surface->get_width(),
				t->target_surface->get_height(),
				t->target_surface->get_id() )
		      : "" ));
		for(Task::List::const_iterator i = t->sub_tasks.begin(); i != t->sub_tasks.end(); ++i)
			log(logfile, *i, use_stack ? optimization_stack : NULL, level+1);
	}
	else
	{
		debug::Log::info(logfile,
			String(level*2, ' ')
			+ (use_stack ? "*" : "")
			+ "NULL" );
	}
}

void
Renderer::log(
	const String &logfile,
	const Task::List &list,
	const String &name,
	const Optimizer::RunParams* optimization_stack ) const
{
	String line = "-------------------------------------------";
	String n = "    " + name;
	n.resize(line.size(), ' ');
	for(int i = 0; i < (int)line.size(); ++i)
		if (n[i] == ' ') n[i] = line[i];
	debug::Log::info(logfile, n);
	for(Task::List::const_iterator i = list.begin(); i != list.end(); ++i)
		log(logfile, *i, optimization_stack);
	debug::Log::info(logfile, line);
}

void
Renderer::initialize()
{
	if (renderers != NULL || queue != NULL)
		synfig::error("rendering::Renderer already initialized");

	// init debug options
	if (const char *s = getenv("SYNFIG_RENDERING_DEBUG_TASK_LIST_LOG"))
		debug_options.task_list_log = s;
	if (const char *s = getenv("SYNFIG_RENDERING_DEBUG_TASK_LIST_OPTIMIZED_LOG"))
		debug_options.task_list_optimized_log = s;
	if (const char *s = getenv("SYNFIG_RENDERING_DEBUG_RESULT_IMAGE"))
		debug_options.result_image = s;

	renderers = new std::map<String, Handle>();
	queue = new Queue();

	initialize_renderers();
}

void
Renderer::deinitialize()
{
	if (renderers == NULL || queue == NULL)
		synfig::error("rendering::Renderer not initialized");

	while(!get_renderers().empty())
		unregister_renderer(get_renderers().begin()->first);

	deinitialize_renderers();

	delete renderers;
	delete queue;
}

void
Renderer::register_renderer(const String &name, const Renderer::Handle &renderer)
{
	if (get_renderer(name))
		synfig::error("rendering::Renderer renderer '%s' already registered", name.c_str());
	(*renderers)[name] = renderer;
}

void
Renderer::unregister_renderer(const String &name)
{
	if (!get_renderer(name))
		synfig::error("rendering::Renderer renderer '%s' not registered", name.c_str());
	renderers->erase(name);
}

const Renderer::Handle&
Renderer::get_renderer(const String &name)
{
	return get_renderers().count(name) > 0
		 ? get_renderers().find(name)->second
		 : blank;
}

const std::map<String, Renderer::Handle>&
Renderer::get_renderers()
{
	if (renderers == NULL)
		synfig::error("rendering::Renderer not initialized");
	return *renderers;
}

/* === E N T R Y P O I N T ================================================= */
