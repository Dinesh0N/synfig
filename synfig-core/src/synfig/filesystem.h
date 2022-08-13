/* === S Y N F I G ========================================================= */
/*!	\file filesystem.h
**	\brief FileSystem
**
**	\legal
**	......... ... 2013 Ivan Mahonin
**
**	This file is part of Synfig.
**
**	Synfig is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 2 of the License, or
**	(at your option) any later version.
**
**	Synfig is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with Synfig.  If not, see <https://www.gnu.org/licenses/>.
**	\endlegal
*/
/* ========================================================================= */

/* === S T A R T =========================================================== */

#ifndef __SYNFIG_FILESYSTEM_H
#define __SYNFIG_FILESYSTEM_H

/* === H E A D E R S ======================================================= */

#include <istream>
#include <ostream>
#include <streambuf>
#include <vector>

#include <ETL/handle>

#include "string.h"

/* === M A C R O S ========================================================= */

/* === T Y P E D E F S ===================================================== */

/* === C L A S S E S & S T R U C T S ======================================= */

namespace synfig
{

	class FileSystem : public etl::shared_object
	{
	public:
		typedef etl::handle<FileSystem> Handle;
		typedef std::vector<String> FileList;

		class Stream : public etl::shared_object
		{
		public:
			typedef etl::handle<Stream> Handle;

		protected:
			FileSystem::Handle file_system_;
			Stream(FileSystem::Handle file_system);
		public:
			virtual ~Stream();
			FileSystem::Handle file_system() const { return file_system_; }
		};

		class ReadStream :
			public Stream,
			private std::streambuf,
			public std::istream
		{
		public:
			typedef etl::handle<ReadStream> Handle;

		protected:
			char buffer_;

			ReadStream(FileSystem::Handle file_system);
			virtual int underflow();
			virtual size_t internal_read(void *buffer, size_t size) = 0;

		public:
			size_t read_block(void *buffer, size_t size)
				{ return read((char*)buffer, size).gcount(); }
			bool read_whole_block(void *buffer, size_t size)
				{ return size == read_block(buffer, size); }
			template<typename T> bool read_variable(T &v)
				{ return read_whole_block(&v, sizeof(T)); }
		};

		class WriteStream :
			public Stream,
			private std::streambuf,
			public std::ostream
		{
		public:
			typedef etl::handle<WriteStream> Handle;

		protected:
			WriteStream(FileSystem::Handle file_system);
	        virtual int overflow(int ch);
			virtual size_t internal_write(const void *buffer, size_t size) = 0;

		public:
			bool write_block(const void *buffer, size_t size)
			{
				for(size_t i = 0; i < size; i++)
					if (!put(((const char*)buffer)[i]).good())
						return i;
				return size;
			}
			bool write_whole_block(const void *buffer, size_t size)
				{ return size == write_block(buffer, size); }
			bool write_whole_stream(std::streambuf &streambuf)
				{ return (*this << &streambuf).good(); }
			bool write_whole_stream(std::istream &stream)
				{ return write_whole_stream(*stream.rdbuf()); }
			bool write_whole_stream(ReadStream::Handle stream)
				{ return !stream || write_whole_stream(*(std::istream*)&(*stream)); }
			template<typename T> bool write_variable(const T &v)
				{ return write_whole_block(&v, sizeof(T)); }
		};

		class Identifier {
		public:
			FileSystem::Handle file_system;
			String filename;
			Identifier() { }
			Identifier(const FileSystem::Handle &file_system, const String &filename):
				file_system(file_system), filename(filename) { }

			bool empty() const { return !file_system; }
			operator bool () const { return !empty(); }

			bool operator < (const Identifier &other) const
			{
				if (file_system < other.file_system) return true;
				if (other.file_system < file_system) return false;
				if (filename < other.filename) return true;
				if (other.filename < filename) return false;
				return false;
			}

			bool operator > (const Identifier &other) const
				{ return other < *this; }
				
			bool operator == (const Identifier &other) const
			{
				return file_system == other.file_system
				    && filename == other.filename;
			}

			bool operator != (const Identifier &other) const
				{ return !(*this == other); }			

			/*bool operator < (const Identifier &other) const
			{
				if (file_system.get() < other.file_system.get()) return true;
				if (other.file_system.get() < file_system.get()) return false;
				if (filename < other.filename) return true;
				if (other.filename < filename) return false;
				return false;
			}
			bool operator > (const Identifier &other) const
				{ return other < *this; }
			bool operator != (const Identifier &other) const
				{ return *this > other || *this < other; }
			bool operator == (const Identifier &other) const
				{ return !(*this != other); }*/

			ReadStream::Handle get_read_stream() const;
			WriteStream::Handle get_write_stream() const;
		};

		FileSystem();
		virtual ~FileSystem();

		virtual bool is_file(const String &filename) = 0;
		virtual bool is_directory(const String &filename) = 0;

		virtual bool directory_create(const String &dirname) = 0;
		virtual bool directory_scan(const String &dirname, FileList &out_files) = 0;

		virtual bool file_remove(const String &filename) = 0;
		virtual bool file_rename(const String &from_filename, const String &to_filename);
		virtual ReadStream::Handle get_read_stream(const String &filename) = 0;
		virtual WriteStream::Handle get_write_stream(const String &filename) = 0;
		virtual String get_real_uri(const String &filename);

		inline bool is_exists(const String filename) { return is_file(filename) || is_directory(filename); }

		String get_real_filename(const String &filename);
		Identifier get_identifier(const String &filename) { return Identifier(this, filename); }

		bool directory_create_recursive(const String &dirname);
		bool remove_recursive(const String &filename);

		static bool copy(Handle from_file_system, const String &from_filename, Handle to_file_system, const String &to_filename);
		static bool copy_recursive(Handle from_file_system, const String &from_filename, Handle to_file_system, const String &to_filename);

		static String fix_slashes(const String &filename);

		///!@brief Read a stream line by line even '\r\n' ended
		static std::istream& safe_get_line(std::istream& is, String& t);
	};

	//! Always empty filesystem (dummy)
	class FileSystemEmpty : public FileSystem
	{
	public:
		typedef etl::handle<FileSystemEmpty> Handle;

		FileSystemEmpty();
		virtual ~FileSystemEmpty();

		virtual bool is_file(const String & /*filename*/)
			{ return false; }
		virtual bool is_directory(const String &filename)
			{ return fix_slashes(filename).empty(); }

		virtual bool directory_create(const String &dirname)
			{ return is_directory(dirname); }
		virtual bool directory_scan(const String &dirname, FileList &out_files)
			{ out_files.clear(); return is_directory(dirname); }

		virtual bool file_remove(const String &filename)
			{ return !is_directory(filename); }
		virtual bool file_rename(const String &from_filename, const String &to_filename)
			{ return is_directory(from_filename) && is_directory(to_filename); }
		virtual FileSystem::ReadStream::Handle get_read_stream(const String &/*filename*/)
			{ return ReadStream::Handle(); }
		virtual FileSystem::WriteStream::Handle get_write_stream(const String &/*filename*/)
			{ return WriteStream::Handle(); }
	};

	namespace filesystem {
		class Path {
		public:
#ifdef _WIN32
			typedef wchar_t	value_type;
#else
			typedef char value_type;
#endif
			typedef std::basic_string<value_type> string_type;

			/**
			 * An empty file system path
			 */
			Path();

			/**
			 * Store a file system path
			 * @param path the path in UTF-8 encoding
			 */
			Path(const std::string& path);

			// Format observers ------------------

			/** Path as a character string in native encoding */
			const value_type* c_str() const noexcept;
			/** Path as a character string in native encoding */
			const string_type& native() const noexcept;
			/** Path as a character string in UTF-8 encoding */
			const std::string& u8string() const;

			// Decomposition ---------------------

			/** Last component of path.
			 *  filename stem + extension
			 */
			Path filename() const;
			/** File name stem.
			 *  @return the substring from the beginning of filename() up to the beginning of extension().
			 *  Dot character of extension is not included.
			 */
			Path stem() const;
			/** File name extension (includes its initial dot if file has extension) */
			Path extension() const;

			// Queries ---------------------------

			bool empty() const noexcept;

			bool has_filename() const;
			bool has_stem() const;
			bool has_extension() const;

		private:
			/** Path in the native encoding */
			string_type native_path_;
			/** Path in UTF-8 encoding */
			std::string path_;

			std::size_t get_filename_pos() const;
			std::size_t get_extension_pos() const;

			/** Convert a UTF-8 encoded string into a native-encoded string
			 *  @param utf8 the string to be converted
			 *  @return a string in native encoding
			 */
			static string_type utf8_to_native(const std::string& utf8);
		};
	}
}

/* === E N D =============================================================== */

#endif
