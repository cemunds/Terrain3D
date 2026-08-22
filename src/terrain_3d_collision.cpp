// Copyright © 2023-2026 Cory Petkovsek, Roope Palmroos, and Contributors.

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/height_map_shape3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/world3d.hpp>

#include <godot_cpp/classes/scene_tree.hpp>

#include <algorithm>

#include "constants.h"
#include "logger.h"
#include "terrain_3d.h"
#include "terrain_3d_collision.h"
#include "terrain_3d_data.h"
#include "terrain_3d_util.h"

///////////////////////////
// Private Functions
///////////////////////////

// Calculates shape data from top left position. Assumes descaled and snapped.
Dictionary Terrain3DCollision::_get_shape_data(const Vector2i &p_position, const int p_size) {
	IS_DATA_INIT_MESG("Terrain not initialized", Dictionary());
	Terrain3DData *data = _terrain->get_data();
	int region_size = _terrain->get_region_size();

	int hshape_size = p_size + 1; // Calculate last vertex at end
	PackedRealArray map_data = PackedRealArray();
	map_data.resize(hshape_size * hshape_size);
	real_t min_height = FLT_MAX;
	real_t max_height = FLT_MIN;

	Ref<Image> map, map_x, map_z, map_xz; // height maps
	Ref<Image> cmap, cmap_x, cmap_z, cmap_xz; // control maps w/ holes

	// Get region_loc of top left corner of descaled and grid snapped collision shape position
	Vector2i region_loc = V2I_DIVIDE_FLOOR(p_position, region_size);
	Ref<Terrain3DRegion> region = data->get_region(region_loc);
	if (region.is_null() || (region.is_valid() && region->is_deleted())) {
		LOG(EXTREME, "Region not found at: ", region_loc, ". Returning blank");
		return Dictionary();
	}
	map = region->get_map(TYPE_HEIGHT);
	cmap = region->get_map(TYPE_CONTROL);

	// Get +X, +Z adjacent regions in case we run over
	region = data->get_region(region_loc + Vector2i(1, 0));
	if (region.is_valid() && !region->is_deleted()) {
		map_x = region->get_map(TYPE_HEIGHT);
		cmap_x = region->get_map(TYPE_CONTROL);
	}
	region = data->get_region(region_loc + Vector2i(0, 1));
	if (region.is_valid() && !region->is_deleted()) {
		map_z = region->get_map(TYPE_HEIGHT);
		cmap_z = region->get_map(TYPE_CONTROL);
	}
	region = data->get_region(region_loc + Vector2i(1, 1));
	if (region.is_valid() && !region->is_deleted()) {
		map_xz = region->get_map(TYPE_HEIGHT);
		cmap_xz = region->get_map(TYPE_CONTROL);
	}

	for (int z = 0; z < hshape_size; z++) {
		for (int x = 0; x < hshape_size; x++) {
			// Choose array indexing to match triangulation of heightmapshape with the mesh
			// https://stackoverflow.com/questions/16684856/rotating-a-2d-pixel-array-by-90-degrees
			// Normal array index rotated Y=0 - shape rotation Y=0 (xform below)
			// int index = z * hshape_size + x;
			// Array Index Rotated Y=-90 - must rotate shape Y=+90 (xform below)
			int index = hshape_size - 1 - z + x * hshape_size;

			Vector2i shape_pos = p_position + Vector2i(x, z);
			Vector2i shape_region_loc = V2I_DIVIDE_FLOOR(shape_pos, region_size);
			int img_x = Math::posmod(shape_pos.x, region_size);
			bool next_x = shape_region_loc.x > region_loc.x;
			int img_y = Math::posmod(shape_pos.y, region_size);
			bool next_z = shape_region_loc.y > region_loc.y;

			// Set heights on local map, or adjacent maps if on the last row/col
			real_t height = 0.f;
			if (!next_x && !next_z && map.is_valid()) {
				height = is_hole(cmap->get_pixel(img_x, img_y).r) ? NAN : map->get_pixel(img_x, img_y).r;
			} else if (next_x && !next_z) {
				if (map_x.is_valid()) {
					height = is_hole(cmap_x->get_pixel(img_x, img_y).r) ? NAN : map_x->get_pixel(img_x, img_y).r;
				} else {
					height = is_hole(cmap->get_pixel(region_size - 1, img_y).r) ? NAN : map->get_pixel(region_size - 1, img_y).r;
				}
			} else if (!next_x && next_z) {
				if (map_z.is_valid()) {
					height = is_hole(cmap_z->get_pixel(img_x, img_y).r) ? NAN : map_z->get_pixel(img_x, img_y).r;
				} else {
					height = (is_hole(cmap->get_pixel(img_x, region_size - 1).r)) ? NAN : map->get_pixel(img_x, region_size - 1).r;
				}
			} else if (next_x && next_z) {
				if (map_xz.is_valid()) {
					height = is_hole(cmap_xz->get_pixel(img_x, img_y).r) ? NAN : map_xz->get_pixel(img_x, img_y).r;
				} else {
					height = (is_hole(cmap->get_pixel(region_size - 1, region_size - 1).r)) ? NAN : map->get_pixel(region_size - 1, region_size - 1).r;
				}
			}
			map_data[index] = height;
			if (!std::isnan(height)) {
				min_height = MIN(min_height, height);
				max_height = MAX(max_height, height);
			}
		}
	}

	// Non rotated shape for normal array index above
	//Transform3D xform = Transform3D(Basis(), global_pos);
	// Rotated shape Y=90 for -90 rotated array index
	Transform3D xform = Transform3D(Basis(Vector3(0, 1.0, 0), Math_PI * .5), v2iv3(p_position + V2I(p_size / 2)));
	Dictionary shape_data;
	shape_data["width"] = hshape_size;
	shape_data["depth"] = hshape_size;
	shape_data["heights"] = map_data;
	shape_data["xform"] = xform;
	shape_data["min_height"] = min_height;
	shape_data["max_height"] = max_height;
	return shape_data;
}

bool Terrain3DCollision::_shape_data_matches(const Dictionary &p_expected, const Variant &p_actual) const {
	if (p_actual.get_type() != Variant::DICTIONARY) {
		return false;
	}
	Dictionary actual = p_actual;
	static const StringName scalar_keys[] = { "width", "depth" };
	for (const StringName &key : scalar_keys) {
		if (!p_expected.has(key) || !actual.has(key) || p_expected[key] != actual[key]) {
			return false;
		}
	}
	if (!p_expected.has("heights") || !actual.has("heights")) {
		return false;
	}
	PackedRealArray expected_heights = p_expected["heights"];
	PackedRealArray actual_heights = actual["heights"];
	if (expected_heights.size() != actual_heights.size()) {
		return false;
	}
	for (int i = 0; i < expected_heights.size(); i++) {
		const real_t expected = expected_heights[i];
		const real_t observed = actual_heights[i];
		if (!(std::isnan(expected) && std::isnan(observed)) && expected != observed) {
			return false;
		}
	}
	return true;
}

void Terrain3DCollision::_shape_set_disabled(const int p_shape_id, const bool p_disabled) {
	if (is_editor_mode()) {
		CollisionShape3D *shape = _shapes[p_shape_id];
		shape->set_disabled(p_disabled);
		shape->set_visible(!p_disabled);
	} else {
		PS->body_set_shape_disabled(_static_body_rid, p_shape_id, p_disabled);
	}
}

void Terrain3DCollision::_shape_set_transform(const int p_shape_id, const Transform3D &p_xform) {
	if (is_editor_mode()) {
		CollisionShape3D *shape = _shapes[p_shape_id];
		shape->set_transform(p_xform);
	} else {
		PS->body_set_shape_transform(_static_body_rid, p_shape_id, p_xform);
	}
}

Vector3 Terrain3DCollision::_shape_get_position(const int p_shape_id) const {
	if (is_editor_mode()) {
		CollisionShape3D *shape = _shapes[p_shape_id];
		return shape->get_global_position();
	} else {
		return PS->body_get_shape_transform(_static_body_rid, p_shape_id).origin;
	}
}

void Terrain3DCollision::_shape_set_data(const int p_shape_id, const Dictionary &p_dict) {
	if (is_editor_mode()) {
		CollisionShape3D *shape = _shapes[p_shape_id];
		Ref<HeightMapShape3D> hshape = shape->get_shape();
		hshape->set_map_data(p_dict["heights"]);
	} else {
		RID shape_rid = PS->body_get_shape(_static_body_rid, p_shape_id);
		PS->shape_set_data(shape_rid, p_dict);
	}
}

void Terrain3DCollision::_reload_physics_material() {
	if (is_editor_mode()) {
		if (_static_body) {
			_static_body->set_physics_material_override(_physics_material);
		}
	} else {
		if (_static_body_rid.is_valid()) {
			if (_physics_material.is_null()) {
				PS->body_set_param(_static_body_rid, PhysicsServer3D::BODY_PARAM_BOUNCE, 0.f);
				PS->body_set_param(_static_body_rid, PhysicsServer3D::BODY_PARAM_FRICTION, 1.f);
			} else {
				real_t computed_bounce = _physics_material->get_bounce() * (_physics_material->is_absorbent() ? -1.f : 1.f);
				real_t computed_friction = _physics_material->get_friction() * (_physics_material->is_rough() ? -1.f : 1.f);
				PS->body_set_param(_static_body_rid, PhysicsServer3D::BODY_PARAM_BOUNCE, computed_bounce);
				PS->body_set_param(_static_body_rid, PhysicsServer3D::BODY_PARAM_FRICTION, computed_friction);
			}
		}
	}
	if (_physics_material.is_valid()) {
		LOG(DEBUG, "Setting PhysicsMaterial bounce: ", _physics_material->get_bounce(), ", friction: ", _physics_material->get_friction());
	}
}

///////////////////////////
// Public Functions
///////////////////////////

void Terrain3DCollision::initialize(Terrain3D *p_terrain) {
	if (p_terrain) {
		_terrain = p_terrain;
	} else {
		return;
	}
	if (!IS_EDITOR && is_editor_mode()) {
		LOG(WARN, "Change collision mode to a non-editor mode for releases");
	}
	build();
}

void Terrain3DCollision::build() {
	IS_DATA_INIT(VOID);
	if (!_terrain->is_inside_world()) {
		LOG(ERROR, "Terrain isn't inside world. Returning.");
		return;
	}

	// Clear collision as the user might change modes in the editor
	destroy();

	// Build only in applicable modes
	if (!is_enabled() || (IS_EDITOR && !is_editor_mode())) {
		return;
	}

	// Create StaticBody3D
	if (is_editor_mode()) {
		LOG(INFO, "Building editor collision");
		_static_body = memnew(StaticBody3D);
		_static_body->set_name("StaticBody3D");
		_static_body->set_as_top_level(true);
		_terrain->add_child(_static_body, true);
		_static_body->set_owner(_terrain);
		_static_body->set_collision_mask(_mask);
		_static_body->set_collision_layer(_layer);
		_static_body->set_collision_priority(_priority);
	} else {
		LOG(INFO, "Building collision with Physics Server");
		_static_body_rid = PS->body_create();
		PS->body_set_mode(_static_body_rid, PhysicsServer3D::BODY_MODE_STATIC);
		PS->body_set_space(_static_body_rid, _terrain->get_world_3d()->get_space());
		PS->body_attach_object_instance_id(_static_body_rid, _terrain->get_instance_id());
		PS->body_set_collision_mask(_static_body_rid, _mask);
		PS->body_set_collision_layer(_static_body_rid, _layer);
		PS->body_set_collision_priority(_static_body_rid, _priority);
	}
	_reload_physics_material();

	// Create CollisionShape3Ds
	int shape_count;
	int hshape_size;
	if (is_dynamic_mode()) {
		int grid_width = _radius * 2 / _shape_size;
		grid_width = int_ceil_pow2(grid_width, 4);
		shape_count = grid_width * grid_width;
		hshape_size = _shape_size + 1;
		LOG(DEBUG, "Grid width: ", grid_width);
	} else {
		shape_count = _terrain->get_data()->get_region_count();
		hshape_size = _terrain->get_region_size() + 1;
	}
	// Preallocate memory for push_back()
	if (is_editor_mode()) {
		_shapes.reserve(shape_count);
	}
	LOG(DEBUG, "Shape count: ", shape_count);
	LOG(DEBUG, "Shape size: ", _shape_size, ", hshape_size: ", hshape_size);
	Transform3D xform(Basis(), V3_MAX);
	for (int i = 0; i < shape_count; i++) {
		if (is_editor_mode()) {
			CollisionShape3D *col_shape = memnew(CollisionShape3D);
			_shapes.push_back(col_shape);
			col_shape->set_name("CollisionShape3D");
			col_shape->set_disabled(true);
			col_shape->set_visible(true);
			Ref<HeightMapShape3D> hshape;
			hshape.instantiate();
			hshape->set_map_width(hshape_size);
			hshape->set_map_depth(hshape_size);
			col_shape->set_shape(hshape);
			_static_body->add_child(col_shape, true);
			col_shape->set_owner(_static_body);
			col_shape->set_transform(xform);
		} else {
			RID shape_rid = PS->heightmap_shape_create();
			PS->body_add_shape(_static_body_rid, shape_rid, xform, true);
			LOG(DEBUG, "Adding shape: ", i, ", rid: ", shape_rid.get_id(), " pos: ", _shape_get_position(i));
		}
	}

	_initialized = true;
	update();
	if (_mode == FULL_GAME) {
		_full_game_region_locations = TypedArray<Vector2i>(_terrain->get_data()->get_region_locations().duplicate());
	}
}

void Terrain3DCollision::update(const bool p_rebuild) {
	if (!_initialized) {
		return;
	}
	if (p_rebuild && !is_dynamic_mode()) {
		build();
		return;
	}
	int time = Time::get_singleton()->get_ticks_usec();
	real_t spacing = _terrain->get_vertex_spacing();

	if (is_dynamic_mode()) {
		// Snap descaled position to a _shape_size grid (eg. multiples of 16)
		Vector2i snapped_pos = _snap_to_grid(_terrain->get_snapped_position() / spacing);
		LOG(EXTREME, "Updating collision at ", snapped_pos);

		// Skip if location hasn't moved to next step
		if (!p_rebuild && (_last_snapped_pos - snapped_pos).length() < _shape_size) {
			return;
		}

		LOG(EXTREME, "---- 1. Defining area as a radius on a grid ----");
		// Create a 0-N grid, center on snapped_pos
		PackedInt32Array grid;
		int grid_width = _radius * 2 / _shape_size; // 64*2/16 = 8
		grid_width = int_ceil_pow2(grid_width, 4);
		grid.resize(grid_width * grid_width);
		grid.fill(-1);
		Vector2i grid_offset = -V2I(grid_width / 2); // offset # cells to center of grid
		Vector2i shape_offset = V2I(_shape_size / 2); // offset meters to top left corner of shape
		Vector2i grid_pos = snapped_pos + grid_offset * _shape_size; // Top left of grid
		LOG(EXTREME, "New Snapped position: ", snapped_pos);
		LOG(EXTREME, "Grid_pos: ", grid_pos);
		LOG(EXTREME, "Radius: ", _radius, ", Grid_width: ", grid_width, ", Grid_offset: ", grid_offset, ", # cells: ", grid.size());
		LOG(EXTREME, "Shape_size: ", _shape_size, ", shape_offset: ", shape_offset);

		LOG(EXTREME, "---- 2. Checking existing shapes ----");
		// If shape is within area, skip
		// Else, mark unused

		// Stores index into _shapes array
		TypedArray<int> inactive_shape_ids;

		int shape_count = is_editor_mode() ? _shapes.size() : PS->body_get_shape_count(_static_body_rid);
		for (int i = 0; i < shape_count; i++) {
			// Descaled global position of shape center
			Vector3 shape_center = _shape_get_position(i) / spacing;
			// Unique key: Top left corner of shape, snapped to grid
			Vector2i shape_pos = _snap_to_grid(v3v2i(shape_center) - shape_offset);
			// Optionally could adjust radius to account for corner (sqrt(_shape_size*2))
			if (!p_rebuild && (shape_center.x < FLT_MAX && v3v2i(shape_center).distance_to(snapped_pos) <= real_t(_radius))) {
				// Get index into shape array
				Vector2i grid_loc = (shape_pos - grid_pos) / _shape_size;
				grid[grid_loc.y * grid_width + grid_loc.x] = i;
				_shape_set_disabled(i, false);
				LOG(EXTREME, "Shape ", i, ": shape_center: ", shape_center.x < FLT_MAX ? shape_center : V3(-999), ", shape_pos: ", shape_pos,
					", grid_loc: ", grid_loc, ", index: ", (grid_loc.y * grid_width + grid_loc.x), " active");
			} else {
				inactive_shape_ids.push_back(i);
				_shape_set_disabled(i, true);
				LOG(EXTREME, "Shape ", i, ": shape_center: ", shape_center.x < FLT_MAX ? shape_center : V3(-999), ", shape_pos: ", shape_pos,
					" out of bounds, marking inactive");
			}
		}
		LOG(EXTREME, "_inactive_shapes size: ", inactive_shape_ids.size());

		LOG(EXTREME, "---- 3. Review grid cells in area ----");
		// If cell is full, skip
		// Else assign shape and form it

		for (int i = 0; i < grid.size(); i++) {
			Vector2i grid_loc(i % grid_width, i / grid_width);
			// Unique key: Top left corner of shape, snapped to grid
			Vector2i shape_pos = grid_pos + grid_loc * _shape_size;

			if ((shape_pos + shape_offset).distance_to(snapped_pos) > real_t(_radius)) {
				LOG(EXTREME, "grid[", i, ":", grid_loc, "] shape_pos : ", shape_pos, " out of circle, skipping");
				continue;
			}
			if (!p_rebuild && grid[i] >= 0) {
				Vector2i center_pos = v3v2i(_shape_get_position(i));
				LOG(EXTREME, "grid[", i, ":", grid_loc, "] shape_pos : ", shape_pos, " act ", center_pos - shape_offset, " Has active shape id: ", grid[i]);
				continue;
			} else {
				if (inactive_shape_ids.size() == 0) {
					LOG(ERROR, "No more unused shapes! Aborting!");
					break;
				}
				Dictionary shape_data = _get_shape_data(shape_pos, _shape_size);
				if (shape_data.is_empty()) {
					LOG(EXTREME, "grid[", i, ":", grid_loc, "] shape_pos : ", shape_pos, " No region found");
					continue;
				}
				int shape_id = inactive_shape_ids.pop_back();
				Transform3D xform = shape_data["xform"];
				LOG(EXTREME, "grid[", i, ":", grid_loc, "] shape_pos : ", shape_pos, " act ", v3v2i(xform.origin) - shape_offset, " placing shape id ", shape_id);
				xform.scale(Vector3(spacing, 1.f, spacing));
				_shape_set_transform(shape_id, xform);
				_shape_set_disabled(shape_id, false);
				_shape_set_data(shape_id, shape_data);
			}
		}
		_last_snapped_pos = snapped_pos;
		LOG(EXTREME, "Setting _last_snapped_pos: ", _last_snapped_pos);
		LOG(EXTREME, "inactive_shape_ids size: ", inactive_shape_ids.size());

	} else {
		// Full collision
		int shape_count = _terrain->get_data()->get_region_count();
		int region_size = _terrain->get_region_size();
		TypedArray<Vector2i> region_locs = _terrain->get_data()->get_region_locations();
		for (int i = 0; i < region_locs.size(); i++) {
			Vector2i region_loc = region_locs[i];
			Vector2i shape_pos = region_loc * region_size;
			Dictionary shape_data = _get_shape_data(shape_pos, region_size);
			if (shape_data.is_empty()) {
				LOG(ERROR, "Can't get shape data for ", region_loc);
				continue;
			}
			Transform3D xform = shape_data["xform"];
			xform.scale(Vector3(spacing, 1.f, spacing));
			_shape_set_transform(i, xform);
			_shape_set_disabled(i, false);
			_shape_set_data(i, shape_data);
		}
	}
	LOG(EXTREME, "Collision update time: ", Time::get_singleton()->get_ticks_usec() - time, " us");
}

Error Terrain3DCollision::rebuild_regions(const TypedArray<Vector2i> &p_region_locations) {
	const uint64_t started = Time::get_singleton()->get_ticks_usec();
	uint64_t preflight_usec = 0;
	uint64_t height_data_usec = 0;
	uint64_t shape_staging_usec = 0;
	uint64_t publication_usec = 0;
	uint64_t verification_usec = 0;
	uint64_t retirement_usec = 0;
	if (_mode != FULL_GAME) {
		return ERR_UNAVAILABLE;
	}
	if (!_initialized || !_terrain || !_static_body_rid.is_valid()) {
		return ERR_UNCONFIGURED;
	}
	Terrain3DData *data = _terrain->get_data();
	if (!data) {
		return ERR_UNCONFIGURED;
	}

	TypedArray<Vector2i> active_locations = data->get_region_locations();
	const int shape_count = PS->body_get_shape_count(_static_body_rid);
	if (shape_count != active_locations.size()) {
		return ERR_INVALID_DATA;
	}
	std::vector<RID> live_rids;
	live_rids.reserve(shape_count);
	for (int slot = 0; slot < shape_count; slot++) {
		const RID rid = PS->body_get_shape(_static_body_rid, slot);
		if (!rid.is_valid() || std::find(live_rids.begin(), live_rids.end(), rid) != live_rids.end()) {
			return ERR_INVALID_DATA;
		}
		live_rids.push_back(rid);
	}

	std::vector<Vector2i> normalized;
	normalized.reserve(p_region_locations.size());
	for (int i = 0; i < p_region_locations.size(); i++) {
		normalized.push_back(p_region_locations[i]);
	}
	std::sort(normalized.begin(), normalized.end(), [](const Vector2i &p_left, const Vector2i &p_right) {
		return p_left.y == p_right.y ? p_left.x < p_right.x : p_left.y < p_right.y;
	});
	normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
	if (normalized.empty()) {
		_last_rebuilt_regions = TypedArray<Vector2i>();
		_last_regional_rebuild_usec = 0;
		_last_regional_rebuild_stage_usec = Dictionary();
		_last_regional_rebuild_stage_usec["preflight"] = 0;
		_last_regional_rebuild_stage_usec["height_data"] = 0;
		_last_regional_rebuild_stage_usec["shape_staging"] = 0;
		_last_regional_rebuild_stage_usec["publication"] = 0;
		_last_regional_rebuild_stage_usec["verification"] = 0;
		_last_regional_rebuild_stage_usec["retirement"] = 0;
		_last_regional_rebuild_stage_usec["total"] = 0;
		return OK;
	}
	if (active_locations != _full_game_region_locations) {
		return ERR_INVALID_DATA;
	}

	struct RegionalShape {
		Vector2i location;
		int slot = -1;
		RID old_rid;
		Transform3D old_transform;
		Dictionary old_data;
		RID new_rid;
		Transform3D new_transform;
		Dictionary new_data;
	};

	std::vector<RegionalShape> staged;
	staged.reserve(normalized.size());
	auto free_staged = [&staged]() {
		for (const RegionalShape &shape : staged) {
			if (shape.new_rid.is_valid()) {
				PS->free_rid(shape.new_rid);
			}
		}
	};
	const int region_size = _terrain->get_region_size();
	const real_t spacing = _terrain->get_vertex_spacing();
	preflight_usec = Time::get_singleton()->get_ticks_usec() - started;
	for (const Vector2i &location : normalized) {
		uint64_t stage_started = Time::get_singleton()->get_ticks_usec();
		const int slot = active_locations.find(location);
		Ref<Terrain3DRegion> region = data->get_region(location);
		if (slot < 0 || slot >= shape_count || region.is_null() || region->is_deleted()) {
			free_staged();
			return ERR_INVALID_PARAMETER;
		}
		const RID old_rid = live_rids[slot];
		const Variant old_data = PS->shape_get_data(old_rid);
		if (!old_rid.is_valid() || old_data.get_type() != Variant::DICTIONARY) {
			free_staged();
			return ERR_INVALID_DATA;
		}
		preflight_usec += Time::get_singleton()->get_ticks_usec() - stage_started;

		stage_started = Time::get_singleton()->get_ticks_usec();
		Dictionary shape_data = _get_shape_data(location * region_size, region_size);
		const int expected_dimension = region_size + 1;
		if (!shape_data.has("width") || !shape_data.has("depth") || !shape_data.has("heights") || !shape_data.has("xform") ||
			int(shape_data["width"]) != expected_dimension || int(shape_data["depth"]) != expected_dimension ||
			PackedRealArray(shape_data["heights"]).size() != expected_dimension * expected_dimension) {
			free_staged();
			return ERR_INVALID_DATA;
		}
		const PackedRealArray heights = shape_data["heights"];
		for (int height_index = 0; height_index < heights.size(); height_index++) {
			if (std::isinf(heights[height_index])) {
				free_staged();
				return ERR_INVALID_DATA;
			}
		}
		Transform3D transform = shape_data["xform"];
		transform.scale(Vector3(spacing, 1.f, spacing));
		if (!transform.is_finite()) {
			free_staged();
			return ERR_INVALID_DATA;
		}
		const Transform3D old_transform = PS->body_get_shape_transform(_static_body_rid, slot);
		if (old_transform != transform) {
			free_staged();
			return ERR_INVALID_DATA;
		}
		height_data_usec += Time::get_singleton()->get_ticks_usec() - stage_started;

		stage_started = Time::get_singleton()->get_ticks_usec();
		RID replacement = PS->heightmap_shape_create();
		if (!replacement.is_valid()) {
			free_staged();
			return ERR_CANT_CREATE;
		}
		staged.push_back({ location, slot, old_rid, old_transform, old_data,
						   replacement, transform, shape_data });
		PS->shape_set_data(replacement, shape_data);
		if (PS->shape_get_type(replacement) != PhysicsServer3D::SHAPE_HEIGHTMAP || !_shape_data_matches(shape_data, PS->shape_get_data(replacement))) {
			free_staged();
			return ERR_INVALID_DATA;
		}
		shape_staging_usec += Time::get_singleton()->get_ticks_usec() - stage_started;
	}

	auto live_matches = [&](const bool p_staged_live) {
		if (PS->body_get_shape_count(_static_body_rid) != shape_count) {
			return false;
		}
		for (const RegionalShape &shape : staged) {
			const RID expected_rid = p_staged_live ? shape.new_rid : shape.old_rid;
			const Transform3D expected_transform = p_staged_live ? shape.new_transform : shape.old_transform;
			const Dictionary expected_data = p_staged_live ? shape.new_data : shape.old_data;
			const RID observed_rid = PS->body_get_shape(_static_body_rid, shape.slot);
			if (observed_rid != expected_rid || PS->body_get_shape_transform(_static_body_rid, shape.slot) != expected_transform ||
				!_shape_data_matches(expected_data, PS->shape_get_data(observed_rid))) {
				return false;
			}
		}
		return true;
	};

	uint64_t stage_started = Time::get_singleton()->get_ticks_usec();
	for (const RegionalShape &shape : staged) {
		PS->body_set_shape(_static_body_rid, shape.slot, shape.new_rid);
		PS->body_set_shape_transform(_static_body_rid, shape.slot, shape.new_transform);
	}
	publication_usec = Time::get_singleton()->get_ticks_usec() - stage_started;
	stage_started = Time::get_singleton()->get_ticks_usec();
	const bool published_matches = live_matches(true);
	verification_usec = Time::get_singleton()->get_ticks_usec() - stage_started;
	if (!published_matches) {
		for (const RegionalShape &shape : staged) {
			PS->body_set_shape(_static_body_rid, shape.slot, shape.old_rid);
			PS->body_set_shape_transform(_static_body_rid, shape.slot, shape.old_transform);
		}
		const bool restored = live_matches(false);
		CRASH_COND_MSG(!restored, "Regional collision rollback failed; refusing to expose mixed live collision");
		free_staged();
		return ERR_INVALID_DATA;
	}

	stage_started = Time::get_singleton()->get_ticks_usec();
	for (const RegionalShape &shape : staged) {
		PS->free_rid(shape.old_rid);
	}
	retirement_usec = Time::get_singleton()->get_ticks_usec() - stage_started;
	TypedArray<Vector2i> rebuilt;
	for (const Vector2i &location : normalized) {
		rebuilt.push_back(location);
	}
	_last_rebuilt_regions = rebuilt;
	_last_regional_rebuild_usec = Time::get_singleton()->get_ticks_usec() - started;
	_last_regional_rebuild_stage_usec = Dictionary();
	_last_regional_rebuild_stage_usec["preflight"] = preflight_usec;
	_last_regional_rebuild_stage_usec["height_data"] = height_data_usec;
	_last_regional_rebuild_stage_usec["shape_staging"] = shape_staging_usec;
	_last_regional_rebuild_stage_usec["publication"] = publication_usec;
	_last_regional_rebuild_stage_usec["verification"] = verification_usec;
	_last_regional_rebuild_stage_usec["retirement"] = retirement_usec;
	_last_regional_rebuild_stage_usec["total"] = _last_regional_rebuild_usec;
	return OK;
}

TypedArray<Vector2i> Terrain3DCollision::get_last_rebuilt_regions() const {
	return TypedArray<Vector2i>(_last_rebuilt_regions.duplicate());
}

Dictionary Terrain3DCollision::get_last_regional_rebuild_stage_usec() const {
	Dictionary stages = _last_regional_rebuild_stage_usec.duplicate();
	if (stages.is_empty()) {
		stages["preflight"] = 0;
		stages["height_data"] = 0;
		stages["shape_staging"] = 0;
		stages["publication"] = 0;
		stages["verification"] = 0;
		stages["retirement"] = 0;
		stages["total"] = 0;
	}
	return stages;
}

void Terrain3DCollision::destroy() {
	_initialized = false;
	_last_snapped_pos = V2I_MAX;
	_full_game_region_locations = TypedArray<Vector2i>();

	// Physics Server
	if (_static_body_rid.is_valid()) {
		// Shape IDs change as they are freed, so it's not safe to iterate over them while freeing.
		while (PS->body_get_shape_count(_static_body_rid) > 0) {
			RID rid = PS->body_get_shape(_static_body_rid, 0);
			LOG(DEBUG, "Freeing CollisionShape RID ", rid);
			PS->free_rid(rid);
		}

		LOG(DEBUG, "Freeing StaticBody RID");
		PS->free_rid(_static_body_rid);
		_static_body_rid = RID();
	}

	// Scene Tree
	for (int i = 0; i < _shapes.size(); i++) {
		CollisionShape3D *shape = _shapes[i];
		LOG(DEBUG, "Freeing CollisionShape3D ", i, " ", shape->get_name());
		remove_from_tree(shape);
		memdelete_safely(shape);
	}
	_shapes.clear();
	if (_static_body) {
		LOG(DEBUG, "Freeing StaticBody3D");
		remove_from_tree(_static_body);
		memdelete_safely(_static_body);
	}
}

void Terrain3DCollision::set_mode(const CollisionMode p_mode) {
	LOG(INFO, "Setting collision mode: ", p_mode);
	if (p_mode != _mode) {
		_mode = p_mode;
		if (is_enabled()) {
			build();
		} else {
			destroy();
		}
	}
}

void Terrain3DCollision::set_shape_size(const uint16_t p_size) {
	int size = CLAMP(p_size, 8, 64);
	size = int_round_mult(size, 8);
	LOG(INFO, "Setting collision dynamic shape size: ", size);
	_shape_size = size;
	// Ensure size:radius always results in at least one valid shape
	if (_shape_size > _radius - 8) {
		set_radius(_shape_size + 16);
	} else if (is_dynamic_mode()) {
		build();
	}
}

void Terrain3DCollision::set_radius(const uint16_t p_radius) {
	int radius = CLAMP(p_radius, 16, 256);
	radius = int_ceil_pow2(radius, 16);
	LOG(INFO, "Setting collision dynamic radius: ", radius);
	_radius = radius;
	// Ensure size:radius always results in at least one valid shape
	if (_radius < _shape_size + 8) {
		set_shape_size(_radius - 8);
	} else if (_shape_size < 16 && _radius > 128) {
		set_shape_size(16);
	} else if (is_dynamic_mode()) {
		build();
	}
}

void Terrain3DCollision::set_layer(const uint32_t p_layers) {
	LOG(INFO, "Setting collision layers: ", p_layers);
	_layer = p_layers;
	if (is_editor_mode()) {
		if (_static_body) {
			_static_body->set_collision_layer(_layer);
		}
	} else {
		if (_static_body_rid.is_valid()) {
			PS->body_set_collision_layer(_static_body_rid, _layer);
		}
	}
}

void Terrain3DCollision::set_mask(const uint32_t p_mask) {
	LOG(INFO, "Setting collision mask: ", p_mask);
	_mask = p_mask;
	if (is_editor_mode()) {
		if (_static_body) {
			_static_body->set_collision_mask(_mask);
		}
	} else {
		if (_static_body_rid.is_valid()) {
			PS->body_set_collision_mask(_static_body_rid, _mask);
		}
	}
}

void Terrain3DCollision::set_priority(const real_t p_priority) {
	LOG(INFO, "Setting collision priority: ", p_priority);
	_priority = p_priority;
	if (is_editor_mode()) {
		if (_static_body) {
			_static_body->set_collision_priority(_priority);
		}
	} else {
		if (_static_body_rid.is_valid()) {
			PS->body_set_collision_priority(_static_body_rid, _priority);
		}
	}
}

void Terrain3DCollision::set_physics_material(const Ref<PhysicsMaterial> &p_mat) {
	LOG(INFO, "Setting physics material: ", p_mat);
	if (_physics_material.is_valid()) {
		if (_physics_material->is_connected("changed", callable_mp(this, &Terrain3DCollision::_reload_physics_material))) {
			LOG(DEBUG, "Disconnecting _physics_material::changed signal to _reload_physics_material()");
			_physics_material->disconnect("changed", callable_mp(this, &Terrain3DCollision::_reload_physics_material));
		}
	}
	_physics_material = p_mat;
	if (_physics_material.is_valid()) {
		LOG(DEBUG, "Connecting _physics_material::changed signal to _reload_physics_material()");
		_physics_material->connect("changed", callable_mp(this, &Terrain3DCollision::_reload_physics_material));
	}
	_reload_physics_material();
}

RID Terrain3DCollision::get_rid() const {
	if (!is_editor_mode()) {
		return _static_body_rid;
	} else {
		if (_static_body) {
			return _static_body->get_rid();
		}
	}
	return RID();
}

///////////////////////////
// Protected Functions
///////////////////////////

void Terrain3DCollision::_bind_methods() {
	BIND_ENUM_CONSTANT(DISABLED);
	BIND_ENUM_CONSTANT(DYNAMIC_GAME);
	BIND_ENUM_CONSTANT(DYNAMIC_EDITOR);
	BIND_ENUM_CONSTANT(FULL_GAME);
	BIND_ENUM_CONSTANT(FULL_EDITOR);

	ClassDB::bind_method(D_METHOD("build"), &Terrain3DCollision::build);
	ClassDB::bind_method(D_METHOD("update", "rebuild"), &Terrain3DCollision::update, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("rebuild_regions", "region_locations"), &Terrain3DCollision::rebuild_regions);
	ClassDB::bind_method(D_METHOD("get_last_rebuilt_regions"), &Terrain3DCollision::get_last_rebuilt_regions);
	ClassDB::bind_method(D_METHOD("get_last_regional_rebuild_usec"), &Terrain3DCollision::get_last_regional_rebuild_usec);
	ClassDB::bind_method(D_METHOD("get_last_regional_rebuild_stage_usec"), &Terrain3DCollision::get_last_regional_rebuild_stage_usec);
	ClassDB::bind_method(D_METHOD("destroy"), &Terrain3DCollision::destroy);
	ClassDB::bind_method(D_METHOD("set_mode", "mode"), &Terrain3DCollision::set_mode);
	ClassDB::bind_method(D_METHOD("get_mode"), &Terrain3DCollision::get_mode);
	ClassDB::bind_method(D_METHOD("is_enabled"), &Terrain3DCollision::is_enabled);
	ClassDB::bind_method(D_METHOD("is_editor_mode"), &Terrain3DCollision::is_editor_mode);
	ClassDB::bind_method(D_METHOD("is_dynamic_mode"), &Terrain3DCollision::is_dynamic_mode);

	ClassDB::bind_method(D_METHOD("set_shape_size", "size"), &Terrain3DCollision::set_shape_size);
	ClassDB::bind_method(D_METHOD("get_shape_size"), &Terrain3DCollision::get_shape_size);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &Terrain3DCollision::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &Terrain3DCollision::get_radius);
	ClassDB::bind_method(D_METHOD("set_layer", "layers"), &Terrain3DCollision::set_layer);
	ClassDB::bind_method(D_METHOD("get_layer"), &Terrain3DCollision::get_layer);
	ClassDB::bind_method(D_METHOD("set_mask", "mask"), &Terrain3DCollision::set_mask);
	ClassDB::bind_method(D_METHOD("get_mask"), &Terrain3DCollision::get_mask);
	ClassDB::bind_method(D_METHOD("set_priority", "priority"), &Terrain3DCollision::set_priority);
	ClassDB::bind_method(D_METHOD("get_priority"), &Terrain3DCollision::get_priority);
	ClassDB::bind_method(D_METHOD("set_physics_material", "material"), &Terrain3DCollision::set_physics_material);
	ClassDB::bind_method(D_METHOD("get_physics_material"), &Terrain3DCollision::get_physics_material);
	ClassDB::bind_method(D_METHOD("get_rid"), &Terrain3DCollision::get_rid);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Disabled,Dynamic / Game,Dynamic / Editor,Full / Game,Full / Editor"), "set_mode", "get_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shape_size", PROPERTY_HINT_RANGE, "8,64,8"), "set_shape_size", "get_shape_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "radius", PROPERTY_HINT_RANGE, "16,256,16"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "layer", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_layer", "get_layer");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_mask", "get_mask");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "priority", PROPERTY_HINT_RANGE, "0.1,256,.1"), "set_priority", "get_priority");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "physics_material", PROPERTY_HINT_RESOURCE_TYPE, "PhysicsMaterial"), "set_physics_material", "get_physics_material");
}
