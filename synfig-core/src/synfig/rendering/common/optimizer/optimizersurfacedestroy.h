/* === S Y N F I G ========================================================= */
/*!	\file synfig/rendering/common/optimizer/optimizersurfacedestroy.h
**	\brief OptimizerSurfaceDestroy Header
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

/* === S T A R T =========================================================== */

#ifndef __SYNFIG_RENDERING_OPTIMIZERSURFACEDESTROY_H
#define __SYNFIG_RENDERING_OPTIMIZERSURFACEDESTROY_H

/* === H E A D E R S ======================================================= */

#include <set>

#include "../../optimizer.h"

/* === M A C R O S ========================================================= */

/* === T Y P E D E F S ===================================================== */

/* === C L A S S E S & S T R U C T S ======================================= */

namespace synfig
{
namespace rendering
{

class OptimizerSurfaceDestroy: public Optimizer
{
private:
	static bool insert_task(
		std::set<Surface::Handle> &destroyed_surfaces,
		Task::List &list,
		Task::List::reverse_iterator &ri,
		const Task::Handle &task );

public:
	virtual bool run(const RunParams &params) const;
};

} /* end namespace rendering */
} /* end namespace synfig */

/* -- E N D ----------------------------------------------------------------- */

#endif