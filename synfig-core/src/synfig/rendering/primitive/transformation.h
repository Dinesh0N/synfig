/* === S Y N F I G ========================================================= */
/*!	\file synfig/rendering/primitive/transformation.h
**	\brief Transformation Header
**
**	$Id$
**
**	\legal
**	......... ... 2015-2018 Ivan Mahonin
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

#ifndef __SYNFIG_RENDERING_TRANSFORMATION_H
#define __SYNFIG_RENDERING_TRANSFORMATION_H

/* === H E A D E R S ======================================================= */

#include "mesh.h"

/* === M A C R O S ========================================================= */

/* === T Y P E D E F S ===================================================== */

/* === C L A S S E S & S T R U C T S ======================================= */

namespace synfig
{
namespace rendering
{


class Transformation: public etl::shared_object
{
public:
	typedef etl::handle<Transformation> Handle;

	struct Bounds {
		Rect rect;
		Vector resolution;

		explicit Bounds(const Rect &rect = Rect(), const Vector &resolution = Vector(1.0, 1.0)):
			rect(rect), resolution(resolution) { }

		inline bool is_valid() const {
			return rect.is_valid()
				&& !rect.is_nan_or_inf()
				&& resolution.is_valid()
				&& !resolution.is_nan_or_inf()
				&& approximate_greater(resolution[0], 0.0)
				&& approximate_greater(resolution[1], 0.0);
		}
	};

protected:
	virtual Transformation::Handle create_inverted_vfunc() const
		{ return Handle(); }

	virtual Point transform_vfunc(const Point &x, bool translate) const = 0;
	virtual Bounds transform_bounds_vfunc(const Bounds &bounds) const = 0;

	virtual Mesh::Handle build_mesh_vfunc(const Rect &target_rect, const Vector &precision) const;

public:
	virtual ~Transformation() { }

	Transformation::Handle create_inverted() const
		{ return create_inverted_vfunc(); }

	Point transform(const Point &x, bool translate = true) const
		{ return transform_vfunc(x, translate); }
	Bounds transform_bounds(const Bounds &bounds) const
		{ return transform_bounds_vfunc(bounds); }
	Bounds transform_bounds(const Rect &bounds) const
		{ return transform_bounds_vfunc(Bounds(bounds)); }
	Bounds transform_bounds(const Rect &bounds, const Vector &resolution) const
		{ return transform_bounds_vfunc(Bounds(bounds, resolution)); }

	Mesh::Handle build_mesh(const Rect &target_rect, const Vector &precision) const;
	Mesh::Handle build_mesh(const Point &target_rect_lt, const Point &target_rect_rb, const Vector &precision) const;
};


//! Transforms all points to NaN
class TransformationVoid: public Transformation
{
public:
	typedef etl::handle<TransformationVoid> Handle;
protected:
	virtual Point transform_vfunc(const Point &/*x*/, bool /*translate*/) const
		{ return Point::nan(); }
	virtual Bounds transform_bounds_vfunc(const Bounds &/*bounds*/) const
		{ return Bounds(); }
};


//! Keeps points unchanged while transformation
class TransformationNone: public Transformation
{
public:
	typedef etl::handle<TransformationNone> Handle;
protected:
	virtual Transformation::Handle create_inverted_vfunc() const
		{ return new TransformationNone(); }
	virtual Point transform_vfunc(const Point &x, bool /*translate*/) const
		{ return x; }
	virtual Bounds transform_bounds_vfunc(const Bounds &bounds) const
		{ return bounds; }
};


} /* end namespace rendering */
} /* end namespace synfig */

/* -- E N D ----------------------------------------------------------------- */

#endif
