/*************************************************************************/
/*  sprite_frames_editor_plugin.cpp                                      */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2022 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2022 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "sprite_frames_editor_plugin.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "core/os/keyboard.h"
#include "editor/editor_file_dialog.h"
#include "editor/editor_file_system.h"
#include "editor/editor_node.h"
#include "editor/editor_scale.h"
#include "editor/editor_settings.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/scene_tree_dock.h"
#include "scene/3d/sprite_3d.h"
#include "scene/gui/center_container.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/separator.h"

static void _draw_shadowed_line(Control *p_control, const Point2 &p_from, const Size2 &p_size, const Size2 &p_shadow_offset, Color p_color, Color p_shadow_color) {
	p_control->draw_line(p_from, p_from + p_size, p_color);
	p_control->draw_line(p_from + p_shadow_offset, p_from + p_size + p_shadow_offset, p_shadow_color);
}

void SpriteFramesEditor::_open_sprite_sheet() {
	file_split_sheet->clear_filters();
	List<String> extensions;
	ResourceLoader::get_recognized_extensions_for_type("Texture2D", &extensions);
	for (int i = 0; i < extensions.size(); i++) {
		file_split_sheet->add_filter("*." + extensions[i]);
	}

	file_split_sheet->popup_file_dialog();
}

int SpriteFramesEditor::_sheet_preview_position_to_frame_index(const Point2 &p_position) {
	const Size2i offset = _get_offset();
	const Size2i frame_size = _get_frame_size();
	const Size2i separation = _get_separation();
	const Size2i block_size = frame_size + separation;
	const Point2i position = p_position / sheet_zoom - offset;

	if (position.x < 0 || position.y < 0) {
		return -1; // Out of bounds.
	}

	if (position.x % block_size.x >= frame_size.x || position.y % block_size.y >= frame_size.y) {
		return -1; // Gap between frames.
	}

	const Point2i frame = position / block_size;
	const Size2i frame_count = _get_frame_count();
	if (frame.x >= frame_count.x || frame.y >= frame_count.y) {
		return -1; // Out of bounds.
	}

	return frame_count.x * frame.y + frame.x;
}

void SpriteFramesEditor::_sheet_preview_draw() {
	const Size2i frame_count = _get_frame_count();
	const Size2i separation = _get_separation();

	const Size2 draw_offset = Size2(_get_offset()) * sheet_zoom;
	const Size2 draw_sep = Size2(separation) * sheet_zoom;
	const Size2 draw_frame_size = Size2(_get_frame_size()) * sheet_zoom;
	const Size2 draw_size = draw_frame_size * frame_count + draw_sep * (frame_count - Size2i(1, 1));

	const Color line_color = Color(1, 1, 1, 0.3);
	const Color shadow_color = Color(0, 0, 0, 0.3);

	// Vertical lines.
	_draw_shadowed_line(split_sheet_preview, draw_offset, Vector2(0, draw_size.y), Vector2(1, 0), line_color, shadow_color);
	for (int i = 0; i < frame_count.x - 1; i++) {
		const Point2 start = draw_offset + Vector2(i * draw_sep.x + (i + 1) * draw_frame_size.x, 0);
		if (separation.x == 0) {
			_draw_shadowed_line(split_sheet_preview, start, Vector2(0, draw_size.y), Vector2(1, 0), line_color, shadow_color);
		} else {
			const Size2 size = Size2(draw_sep.x, draw_size.y);
			split_sheet_preview->draw_rect(Rect2(start, size), line_color);
		}
	}
	_draw_shadowed_line(split_sheet_preview, draw_offset + Vector2(draw_size.x, 0), Vector2(0, draw_size.y), Vector2(1, 0), line_color, shadow_color);

	// Horizontal lines.
	_draw_shadowed_line(split_sheet_preview, draw_offset, Vector2(draw_size.x, 0), Vector2(0, 1), line_color, shadow_color);
	for (int i = 0; i < frame_count.y - 1; i++) {
		const Point2 start = draw_offset + Vector2(0, i * draw_sep.y + (i + 1) * draw_frame_size.y);
		if (separation.y == 0) {
			_draw_shadowed_line(split_sheet_preview, start, Vector2(draw_size.x, 0), Vector2(0, 1), line_color, shadow_color);
		} else {
			const Size2 size = Size2(draw_size.x, draw_sep.y);
			split_sheet_preview->draw_rect(Rect2(start, size), line_color);
		}
	}
	_draw_shadowed_line(split_sheet_preview, draw_offset + Vector2(0, draw_size.y), Vector2(draw_size.x, 0), Vector2(0, 1), line_color, shadow_color);

	if (frames_selected.size() == 0) {
		split_sheet_dialog->get_ok_button()->set_disabled(true);
		split_sheet_dialog->set_ok_button_text(TTR("No Frames Selected"));
		return;
	}

	Color accent = get_theme_color("accent_color", "Editor");

	for (const int &E : frames_selected) {
		const int idx = E;
		const int x = idx % frame_count.x;
		const int y = idx / frame_count.x;
		const Point2 pos = draw_offset + Point2(x, y) * (draw_frame_size + draw_sep);
		split_sheet_preview->draw_rect(Rect2(pos + Size2(5, 5), draw_frame_size - Size2(10, 10)), Color(0, 0, 0, 0.35), true);
		split_sheet_preview->draw_rect(Rect2(pos, draw_frame_size), Color(0, 0, 0, 1), false);
		split_sheet_preview->draw_rect(Rect2(pos + Size2(1, 1), draw_frame_size - Size2(2, 2)), Color(0, 0, 0, 1), false);
		split_sheet_preview->draw_rect(Rect2(pos + Size2(2, 2), draw_frame_size - Size2(4, 4)), accent, false);
		split_sheet_preview->draw_rect(Rect2(pos + Size2(3, 3), draw_frame_size - Size2(6, 6)), accent, false);
		split_sheet_preview->draw_rect(Rect2(pos + Size2(4, 4), draw_frame_size - Size2(8, 8)), Color(0, 0, 0, 1), false);
		split_sheet_preview->draw_rect(Rect2(pos + Size2(5, 5), draw_frame_size - Size2(10, 10)), Color(0, 0, 0, 1), false);
	}

	split_sheet_dialog->get_ok_button()->set_disabled(false);
	split_sheet_dialog->set_ok_button_text(vformat(TTR("Add %d Frame(s)"), frames_selected.size()));
}

void SpriteFramesEditor::_sheet_preview_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
		const int idx = _sheet_preview_position_to_frame_index(mb->get_position());

		if (idx != -1) {
			if (mb->is_shift_pressed() && last_frame_selected >= 0) {
				//select multiple
				int from = idx;
				int to = last_frame_selected;
				if (from > to) {
					SWAP(from, to);
				}

				for (int i = from; i <= to; i++) {
					// Prevent double-toggling the same frame when moving the mouse when the mouse button is still held.
					frames_toggled_by_mouse_hover.insert(idx);

					if (mb->is_ctrl_pressed()) {
						frames_selected.erase(i);
					} else {
						frames_selected.insert(i);
					}
				}
			} else {
				// Prevent double-toggling the same frame when moving the mouse when the mouse button is still held.
				frames_toggled_by_mouse_hover.insert(idx);

				if (frames_selected.has(idx)) {
					frames_selected.erase(idx);
				} else {
					frames_selected.insert(idx);
				}
			}
		}

		if (last_frame_selected != idx || idx != -1) {
			last_frame_selected = idx;
			split_sheet_preview->queue_redraw();
		}
	}

	if (mb.is_valid() && !mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
		frames_toggled_by_mouse_hover.clear();
	}

	const Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && (mm->get_button_mask() & MouseButton::MASK_LEFT) != MouseButton::NONE) {
		// Select by holding down the mouse button on frames.
		const int idx = _sheet_preview_position_to_frame_index(mm->get_position());

		if (idx != -1 && !frames_toggled_by_mouse_hover.has(idx)) {
			// Only allow toggling each tile once per mouse hold.
			// Otherwise, the selection would constantly "flicker" in and out when moving the mouse cursor.
			// The mouse button must be released before it can be toggled again.
			frames_toggled_by_mouse_hover.insert(idx);

			if (frames_selected.has(idx)) {
				frames_selected.erase(idx);
			} else {
				frames_selected.insert(idx);
			}

			last_frame_selected = idx;
			split_sheet_preview->queue_redraw();
		}
	}
}

void SpriteFramesEditor::_sheet_scroll_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventMouseButton> mb = p_event;

	if (mb.is_valid()) {
		// Zoom in/out using Ctrl + mouse wheel. This is done on the ScrollContainer
		// to allow performing this action anywhere, even if the cursor isn't
		// hovering the texture in the workspace.
		if (mb->get_button_index() == MouseButton::WHEEL_UP && mb->is_pressed() && mb->is_ctrl_pressed()) {
			_sheet_zoom_on_position(scale_ratio, mb->get_position());
			// Don't scroll up after zooming in.
			split_sheet_scroll->accept_event();
		} else if (mb->get_button_index() == MouseButton::WHEEL_DOWN && mb->is_pressed() && mb->is_ctrl_pressed()) {
			_sheet_zoom_on_position(1 / scale_ratio, mb->get_position());
			// Don't scroll down after zooming out.
			split_sheet_scroll->accept_event();
		}
	}

	const Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_valid() && (mm->get_button_mask() & MouseButton::MASK_MIDDLE) != MouseButton::NONE) {
		const Vector2 dragged = Input::get_singleton()->warp_mouse_motion(mm, split_sheet_scroll->get_global_rect());
		split_sheet_scroll->set_h_scroll(split_sheet_scroll->get_h_scroll() - dragged.x);
		split_sheet_scroll->set_v_scroll(split_sheet_scroll->get_v_scroll() - dragged.y);
	}
}

void SpriteFramesEditor::_sheet_add_frames() {
	const Size2i frame_count = _get_frame_count();
	const Size2i frame_size = _get_frame_size();
	const Size2i offset = _get_offset();
	const Size2i separation = _get_separation();

	Ref<EditorUndoRedoManager> &undo_redo = EditorNode::get_undo_redo();
	undo_redo->create_action(TTR("Add Frame"));

	int fc = frames->get_frame_count(edited_anim);

	for (const int &E : frames_selected) {
		int idx = E;
		const Point2 frame_coords(idx % frame_count.x, idx / frame_count.x);

		Ref<AtlasTexture> at;
		at.instantiate();
		at->set_atlas(split_sheet_preview->get_texture());
		at->set_region(Rect2(offset + frame_coords * (frame_size + separation), frame_size));

		undo_redo->add_do_method(frames, "add_frame", edited_anim, at, -1);
		undo_redo->add_undo_method(frames, "remove_frame", edited_anim, fc);
	}

	undo_redo->add_do_method(this, "_update_library");
	undo_redo->add_undo_method(this, "_update_library");
	undo_redo->commit_action();
}

void SpriteFramesEditor::_sheet_zoom_on_position(float p_zoom, const Vector2 &p_position) {
	const float old_zoom = sheet_zoom;
	sheet_zoom = CLAMP(sheet_zoom * p_zoom, min_sheet_zoom, max_sheet_zoom);

	const Size2 texture_size = split_sheet_preview->get_texture()->get_size();
	split_sheet_preview->set_custom_minimum_size(texture_size * sheet_zoom);

	Vector2 offset = Vector2(split_sheet_scroll->get_h_scroll(), split_sheet_scroll->get_v_scroll());
	offset = (offset + p_position) / old_zoom * sheet_zoom - p_position;
	split_sheet_scroll->set_h_scroll(offset.x);
	split_sheet_scroll->set_v_scroll(offset.y);
}

void SpriteFramesEditor::_sheet_zoom_in() {
	_sheet_zoom_on_position(scale_ratio, Vector2());
}

void SpriteFramesEditor::_sheet_zoom_out() {
	_sheet_zoom_on_position(1 / scale_ratio, Vector2());
}

void SpriteFramesEditor::_sheet_zoom_reset() {
	// Default the zoom to match the editor scale, but don't dezoom on editor scales below 100% to prevent pixel art from looking bad.
	sheet_zoom = MAX(1.0f, EDSCALE);
	Size2 texture_size = split_sheet_preview->get_texture()->get_size();
	split_sheet_preview->set_custom_minimum_size(texture_size * sheet_zoom);
}

void SpriteFramesEditor::_sheet_select_clear_all_frames() {
	bool should_clear = true;
	for (int i = 0; i < split_sheet_h->get_value() * split_sheet_v->get_value(); i++) {
		if (!frames_selected.has(i)) {
			frames_selected.insert(i);
			should_clear = false;
		}
	}
	if (should_clear) {
		frames_selected.clear();
	}

	split_sheet_preview->queue_redraw();
}

void SpriteFramesEditor::_sheet_spin_changed(double p_value, int p_dominant_param) {
	if (updating_split_settings) {
		return;
	}
	updating_split_settings = true;

	if (p_dominant_param != PARAM_USE_CURRENT) {
		dominant_param = p_dominant_param;
	}

	const Size2i texture_size = split_sheet_preview->get_texture()->get_size();
	const Size2i size = texture_size - _get_offset();

	switch (dominant_param) {
		case PARAM_SIZE: {
			const Size2i frame_size = _get_frame_size();

			const Size2i offset_max = texture_size - frame_size;
			split_sheet_offset_x->set_max(offset_max.x);
			split_sheet_offset_y->set_max(offset_max.y);

			const Size2i sep_max = size - frame_size * 2;
			split_sheet_sep_x->set_max(sep_max.x);
			split_sheet_sep_y->set_max(sep_max.y);

			const Size2i separation = _get_separation();
			const Size2i count = (size + separation) / (frame_size + separation);
			split_sheet_h->set_value(count.x);
			split_sheet_v->set_value(count.y);
		} break;

		case PARAM_FRAME_COUNT: {
			const Size2i count = _get_frame_count();

			const Size2i offset_max = texture_size - count;
			split_sheet_offset_x->set_max(offset_max.x);
			split_sheet_offset_y->set_max(offset_max.y);

			const Size2i gap_count = count - Size2i(1, 1);
			split_sheet_sep_x->set_max(gap_count.x == 0 ? size.x : (size.x - count.x) / gap_count.x);
			split_sheet_sep_y->set_max(gap_count.y == 0 ? size.y : (size.y - count.y) / gap_count.y);

			const Size2i separation = _get_separation();
			const Size2i frame_size = (size - separation * gap_count) / count;
			split_sheet_size_x->set_value(frame_size.x);
			split_sheet_size_y->set_value(frame_size.y);
		} break;
	}

	updating_split_settings = false;

	frames_selected.clear();
	last_frame_selected = -1;
	split_sheet_preview->queue_redraw();
}

void SpriteFramesEditor::_prepare_sprite_sheet(const String &p_file) {
	Ref<Texture2D> texture = ResourceLoader::load(p_file);
	if (!texture.is_valid()) {
		EditorNode::get_singleton()->show_warning(TTR("Unable to load images"));
		ERR_FAIL_COND(!texture.is_valid());
	}
	frames_selected.clear();
	last_frame_selected = -1;

	bool new_texture = texture != split_sheet_preview->get_texture();
	split_sheet_preview->set_texture(texture);
	if (new_texture) {
		// Reset spin max.
		const Size2i size = texture->get_size();
		split_sheet_size_x->set_max(size.x);
		split_sheet_size_y->set_max(size.y);
		split_sheet_sep_x->set_max(size.x);
		split_sheet_sep_y->set_max(size.y);
		split_sheet_offset_x->set_max(size.x);
		split_sheet_offset_y->set_max(size.y);

		// Different texture, reset to 4x4.
		dominant_param = PARAM_FRAME_COUNT;
		updating_split_settings = true;
		split_sheet_h->set_value(4);
		split_sheet_v->set_value(4);
		split_sheet_size_x->set_value(size.x / 4);
		split_sheet_size_y->set_value(size.y / 4);
		split_sheet_sep_x->set_value(0);
		split_sheet_sep_y->set_value(0);
		split_sheet_offset_x->set_value(0);
		split_sheet_offset_y->set_value(0);
		updating_split_settings = false;

		// Reset zoom.
		_sheet_zoom_reset();
	}
	split_sheet_dialog->popup_centered_ratio(0.65);
}

void SpriteFramesEditor::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
		case NOTIFICATION_THEME_CHANGED: {
			load->set_icon(get_theme_icon(SNAME("Load"), SNAME("EditorIcons")));
			load_sheet->set_icon(get_theme_icon(SNAME("SpriteSheet"), SNAME("EditorIcons")));
			copy->set_icon(get_theme_icon(SNAME("ActionCopy"), SNAME("EditorIcons")));
			paste->set_icon(get_theme_icon(SNAME("ActionPaste"), SNAME("EditorIcons")));
			empty_before->set_icon(get_theme_icon(SNAME("InsertBefore"), SNAME("EditorIcons")));
			empty_after->set_icon(get_theme_icon(SNAME("InsertAfter"), SNAME("EditorIcons")));
			move_up->set_icon(get_theme_icon(SNAME("MoveLeft"), SNAME("EditorIcons")));
			move_down->set_icon(get_theme_icon(SNAME("MoveRight"), SNAME("EditorIcons")));
			delete_frame->set_icon(get_theme_icon(SNAME("Remove"), SNAME("EditorIcons")));
			zoom_out->set_icon(get_theme_icon(SNAME("ZoomLess"), SNAME("EditorIcons")));
			zoom_reset->set_icon(get_theme_icon(SNAME("ZoomReset"), SNAME("EditorIcons")));
			zoom_in->set_icon(get_theme_icon(SNAME("ZoomMore"), SNAME("EditorIcons")));
			add_anim->set_icon(get_theme_icon(SNAME("New"), SNAME("EditorIcons")));
			delete_anim->set_icon(get_theme_icon(SNAME("Remove"), SNAME("EditorIcons")));
			anim_search_box->set_right_icon(get_theme_icon(SNAME("Search"), SNAME("EditorIcons")));
			split_sheet_zoom_out->set_icon(get_theme_icon(SNAME("ZoomLess"), SNAME("EditorIcons")));
			split_sheet_zoom_reset->set_icon(get_theme_icon(SNAME("ZoomReset"), SNAME("EditorIcons")));
			split_sheet_zoom_in->set_icon(get_theme_icon(SNAME("ZoomMore"), SNAME("EditorIcons")));
			split_sheet_scroll->add_theme_style_override("panel", get_theme_stylebox(SNAME("panel"), SNAME("Tree")));
		} break;

		case NOTIFICATION_READY: {
			add_theme_constant_override("autohide", 1); // Fixes the dragger always showing up.
		} break;
	}
}

void SpriteFramesEditor::_file_load_request(const Vector<String> &p_path, int p_at_pos) {
	ERR_FAIL_COND(!frames->has_animation(edited_anim));

	List<Ref<Texture2D>> resources;

	for (int i = 0; i < p_path.size(); i++) {
		Ref<Texture2D> resource;
		resource = ResourceLoader::load(p_path[i]);

		if (resource.is_null()) {
			dialog->set_text(TTR("ERROR: Couldn't load frame resource!"));
			dialog->set_title(TTR("Error!"));

			//dialog->get_cancel()->set_text("Close");
			dialog->set_ok_button_text(TTR("Close"));
			dialog->popup_centered();
			return; ///beh should show an error i guess
		}

		resources.push_back(resource);
	}

	if (resources.is_empty()) {
		return;
	}

	Ref<EditorUndoRedoManager> &undo_redo = EditorNode::get_undo_redo();
	undo_redo->create_action(TTR("Add Frame"));
	int fc = frames->get_frame_count(edited_anim);

	int count = 0;

	for (const Ref<Texture2D> &E : resources) {
		undo_redo->add_do_method(frames, "add_frame", edited_anim, E, p_at_pos == -1 ? -1 : p_at_pos + count);
		undo_redo->add_undo_method(frames, "remove_frame", edited_anim, p_at_pos == -1 ? fc : p_at_pos);
		count++;
	}
	undo_redo->add_do_method(this, "_update_library");
	undo_redo->add_undo_method(this, "_update_library");

	undo_redo->commit_action();
}

Size2i SpriteFramesEditor::_get_frame_count() const {
	return Size2i(split_sheet_h->get_value(), split_sheet_v->get_value());
}

Size2i SpriteFramesEditor::_get_frame_size() const {
	return Size2i(split_sheet_size_x->get_value(), split_sheet_size_y->get_value());
}

Size2i SpriteFramesEditor::_get_offset() const {
	return Size2i(split_sheet_offset_x->get_value(), split_sheet_offset_y->get_value());
}

Size2i SpriteFramesEditor::_get_separation() const {
	return Size2i(split_sheet_sep_x->get_value(), split_sheet_sep_y->get_value());
}

void SpriteFramesEditor::_load_pressed() {
	ERR_FAIL_COND(!frames->has_animation(edited_anim));
	loading_scene = false;

	file->clear_filters();
	List<String> extensions;
	ResourceLoader::get_recognized_extensions_for_type("Texture2D", &extensions);
	for (int i = 0; i < extensions.size(); i++) {
		file->add_filter("*." + extensions[i]);
	}

	file->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILES);
	file->popup_file_dialog();
}

void SpriteFramesEditor::_paste_pressed() {
	ERR_FAIL_COND(!frames->has_animation(edited_anim));

	Ref<Texture2D> r = EditorSettings::get_singleton()->get_resource_clipboard();
	if (!r.is_valid()) {
		dialog->set_text(TTR("Resource clipboard is empty or not a texture!"));
		dialog->set_title(TTR("Error!"));
		//dialog->get_cancel()->set_text("Close");
		dialog->set_ok_button_text(TTR("Close"));
		dialog->popup_centered();
		return; ///beh should show an error i guess
	}

	Ref<EditorUndoRedoManager> &undo_redo = EditorNode::get_undo_redo();
	undo_redo->create_action(TTR("Paste Frame"));
	undo_redo->add_do_method(frames, "add_frame", edited_anim, r);
	undo_redo->add_undo_method(frames, "remove_frame", edited_anim, frames->get_frame_count(edited_anim));
	undo_redo->add_do_method(this, "_update_library");
	undo_redo->add_undo_method(this, "_update_library");
	undo_redo->commit_action();
}

void SpriteFramesEditor::_copy_pressed() {
	ERR_FAIL_COND(!frames->has_animation(edited_anim));

	if (tree->get_current() < 0) {
		return;
	}
	Ref<Texture2D> r = frames->get_frame(edited_anim, tree->get_current());
	if (!r.is_valid()) {
		return;
	}

	EditorSettings::get_singleton()->set_resource_clipboard(r);
}

void SpriteFramesEditor::_empty_pressed() {
	ERR_FAIL_COND(!frames->has_animation(edited_anim));

	int from = -1;

	if (tree->get_current() >= 0) {
		from = tree->get_current();
		sel = from;

	} else {
		from = frames->get_frame_count(edited_anim);
	}

	Ref<Texture2D> r;

	Ref<EditorUndoRedoManager> &undo_redo = EditorNode::get_undo_redo();
	undo_redo->create_action(TTR("Add Empty"));
	undo_redo->add_do_method(frames, "add_frame", edited_anim, r, from);
	undo_redo->add_undo_method(frames, "remove_frame", edited_anim, from);
	undo_redo->add_do_method(this, "_update_library");
	undo_redo->add_undo_method(this, "_update_library");
	undo_redo->commit_action();
}

void SpriteFramesEditor::_empty2_pressed() {
	ERR_FAIL_COND(!frames->has_animation(edited_anim));

	int from = -1;

	if (tree->get_current() >= 0) {
		from = tree->get_current();
		sel = from;

	} else {
		from = frames->get_frame_count(edited_anim);
	}

	Ref<Texture2D> r;

	Ref<EditorUndoRedoManager> &undo_redo = EditorNode::get_undo_redo();
	undo_redo->create_action(TTR("Add Empty"));
	undo_redo->add_do_method(frames, "add_frame", edited_anim, r, from + 1);
	undo_redo->add_undo_method(frames, "remove_frame", edited_anim, from + 1);
	undo_redo->add_do_method(this, "_update_library");
	undo_redo->add_undo_method(this, "_update_library");
	undo_redo->commit_action();
}

void SpriteFramesEditor::_up_pressed() {
	ERR_FAIL_COND(!frames->has_animation(edited_anim));

	if (tree->get_current() < 0) {
		return;
	}

	int to_move = tree->get_current();
	if (to_move < 1) {
		return;
	}

	sel = to_move;
	sel -= 1;

	Ref<EditorUndoRedoManager> &undo_redo = EditorNode::get_undo_redo();
	undo_redo->create_action(TTR("Delete Resource"));
	undo_redo->add_do_method(frames, "set_frame", edited_anim, to_move, frames->get_frame(edited_anim, to_move - 1));
	undo_redo->add_do_method(frames, "set_frame", edited_anim, to_move - 1, frames->get_frame(edited_anim, to_move));
	undo_redo->add_undo_method(frames, "set_frame", edited_anim, to_move, frames->get_frame(edited_anim, to_move));
	undo_redo->add_undo_method(frames, "set_frame", edited_anim, to_move - 1, frames->get_frame(edited_anim, to_move - 1));
	undo_redo->add_do_method(this, "_update_library");
	undo_redo->add_undo_method(this, "_update_library");
	undo_redo->commit_action();
}

void SpriteFramesEditor::_down_pressed() {
	ERR_FAIL_COND(!frames->has_animation(edited_anim));

	if (tree->get_current() < 0) {
		return;
	}

	int to_move = tree->get_current();
	if (to_move < 0 || to_move >= frames->get_frame_count(edited_anim) - 1) {
		return;
	}

	sel = to_move;
	sel += 1;

	Ref<EditorUndoRedoManager> &undo_redo = EditorNode::get_undo_redo();
	undo_redo->create_action(TTR("Delete Resource"));
	undo_redo->add_do_method(frames, "set_frame", edited_anim, to_move, frames->get_frame(edited_anim, to_move + 1));
	undo_redo->add_do_method(frames, "set_frame", edited_anim, to_move + 1, frames->get_frame(edited_anim, to_move));
	undo_redo->add_undo_method(frames, "set_frame", edited_anim, to_move, frames->get_frame(edited_anim, to_move));
	undo_redo->add_undo_method(frames, "set_frame", edited_anim, to_move + 1, frames->get_frame(edited_anim, to_move + 1));
	undo_redo->add_do_method(this, "_update_library");
	undo_redo->add_undo_method(this, "_update_library");
	undo_redo->commit_action();
}

void SpriteFramesEditor::_delete_pressed() {
	ERR_FAIL_COND(!frames->has_animation(edited_anim));

	if (tree->get_current() < 0) {
		return;
	}

	int to_delete = tree->get_current();
	if (to_delete < 0 || to_delete >= frames->get_frame_count(edited_anim)) {
		return;
	}

	Ref<EditorUndoRedoManager> &undo_redo = EditorNode::get_undo_redo();
	undo_redo->create_action(TTR("Delete Resource"));
	undo_redo->add_do_method(frames, "remove_frame", edited_anim, to_delete);
	undo_redo->add_undo_method(frames, "add_frame", edited_anim, frames->get_frame(edited_anim, to_delete), to_delete);
	undo_redo->add_do_method(this, "_update_library");
	undo_redo->add_undo_method(this, "_update_library");
	undo_redo->commit_action();
}

void SpriteFramesEditor::_animation_select() {
	if (updating) {
		return;
	}

	if (frames->has_animation(edited_anim)) {
		double value = anim_speed->get_line_edit()->get_text().to_float();
		if (!Math::is_equal_approx(value, (double)frames->get_animation_speed(edited_anim))) {
			_animation_fps_changed(value);
		}
	}

	TreeItem *selected = animations->get_selected();
	ERR_FAIL_COND(!selected);
	edited_anim = selected->get_text(0);
	_update_library(true);
}

static void _find_anim_sprites(Node *p_node, List<Node *> *r_nodes, Ref<SpriteFrames> p_sfames) {
	Node *edited = EditorNode::get_singleton()->get_edited_scene();
	if (!edited) {
		return;
	}
	if (p_node != edited && p_node->get_owner() != edited) {
		return;
	}

	{
		AnimatedSprite2D *as = Object::cast_to<AnimatedSprite2D>(p_node);
		if (as && as->get_sprite_frames() == p_sfames) {
			r_nodes->push_back(p_node);
		}
	}

	{
		AnimatedSprite3D *as = Object::cast_to<AnimatedSprite3D>(p_node);
		if (as && as->get_sprite_frames() == p_sfames) {
			r_nodes->push_back(p_node);
		}
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_find_anim_sprites(p_node->get_child(i), r_nodes, p_sfames);
	}
}

void SpriteFramesEditor::_animation_name_edited() {
	if (updating) {
		return;
	}

	if (!frames->has_animation(edited_anim)) {
		return;
	}

	TreeItem *edited = animations->get_edited();
	if (!edited) {
		return;
	}

	String new_name = edited->get_text(0);

	if (new_name == String(edited_anim)) {
		return;
	}

	new_name = new_name.replace("/", "_").replace(",", " ");

	String name = new_name;
	int counter = 0;
	while (frames->has_animation(name)) {
		counter++;
		name = new_name + " " + itos(counter);
	}

	List<Node *> nodes;
	_find_anim_sprites(EditorNode::get_singleton()->get_edited_scene(), &nodes, Ref<SpriteFrames>(frames));

	Ref<EditorUndoRedoManager> &undo_redo = EditorNode::get_undo_redo();
	undo_redo->create_action(TTR("Rename Animation"));
	undo_redo->add_do_method(frames, "rename_animation", edited_anim, name);
	undo_redo->add_undo_method(frames, "rename_animation", name, edited_anim);

	for (Node *E : nodes) {
		String current = E->call("get_animation");
		undo_redo->add_do_method(E, "set_animation", name);
		undo_redo->add_undo_method(E, "set_animation", edited_anim);
	}

	undo_redo->add_do_method(this, "_update_library");
	undo_redo->add_undo_method(this, "_update_library");

	edited_anim = name;

	undo_redo->commit_action();
}

void SpriteFramesEditor::_animation_add() {
	String name = "New Anim";
	int counter = 0;
	while (frames->has_animation(name)) {
		counter++;
		name = "New Anim " + itos(counter);
	}

	List<Node *> nodes;
	_find_anim_sprites(EditorNode::get_singleton()->get_edited_scene(), &nodes, Ref<SpriteFrames>(frames));

	Ref<EditorUndoRedoManager> &undo_redo = EditorNode::get_undo_redo();
	undo_redo->create_action(TTR("Add Animation"));
	undo_redo->add_do_method(frames, "add_animation", name);
	undo_redo->add_undo_method(frames, "remove_animation", name);
	undo_redo->add_do_method(this, "_update_library");
	undo_redo->add_undo_method(this, "_update_library");

	for (Node *E : nodes) {
		String current = E->call("get_animation");
		undo_redo->add_do_method(E, "set_animation", name);
		undo_redo->add_undo_method(E, "set_animation", current);
	}

	edited_anim = name;

	undo_redo->commit_action();
	animations->grab_focus();
}

void SpriteFramesEditor::_animation_remove() {
	if (updating) {
		return;
	}

	if (!frames->has_animation(edited_anim)) {
		return;
	}

	delete_dialog->set_text(TTR("Delete Animation?"));
	delete_dialog->popup_centered();
}

void SpriteFramesEditor::_animation_remove_confirmed() {
	Ref<EditorUndoRedoManager> &undo_redo = EditorNode::get_undo_redo();
	undo_redo->create_action(TTR("Remove Animation"));
	undo_redo->add_do_method(frames, "remove_animation", edited_anim);
	undo_redo->add_undo_method(frames, "add_animation", edited_anim);
	undo_redo->add_undo_method(frames, "set_animation_speed", edited_anim, frames->get_animation_speed(edited_anim));
	undo_redo->add_undo_method(frames, "set_animation_loop", edited_anim, frames->get_animation_loop(edited_anim));
	int fc = frames->get_frame_count(edited_anim);
	for (int i = 0; i < fc; i++) {
		Ref<Texture2D> frame = frames->get_frame(edited_anim, i);
		undo_redo->add_undo_method(frames, "add_frame", edited_anim, frame);
	}
	undo_redo->add_do_method(this, "_update_library");
	undo_redo->add_undo_method(this, "_update_library");

	edited_anim = StringName();

	undo_redo->commit_action();
}

void SpriteFramesEditor::_animation_search_text_changed(const String &p_text) {
	_update_library();
}

void SpriteFramesEditor::_animation_loop_changed() {
	if (updating) {
		return;
	}

	Ref<EditorUndoRedoManager> &undo_redo = EditorNode::get_undo_redo();
	undo_redo->create_action(TTR("Change Animation Loop"));
	undo_redo->add_do_method(frames, "set_animation_loop", edited_anim, anim_loop->is_pressed());
	undo_redo->add_undo_method(frames, "set_animation_loop", edited_anim, frames->get_animation_loop(edited_anim));
	undo_redo->add_do_method(this, "_update_library", true);
	undo_redo->add_undo_method(this, "_update_library", true);
	undo_redo->commit_action();
}

void SpriteFramesEditor::_animation_fps_changed(double p_value) {
	if (updating) {
		return;
	}

	Ref<EditorUndoRedoManager> &undo_redo = EditorNode::get_undo_redo();
	undo_redo->create_action(TTR("Change Animation FPS"), UndoRedo::MERGE_ENDS);
	undo_redo->add_do_method(frames, "set_animation_speed", edited_anim, p_value);
	undo_redo->add_undo_method(frames, "set_animation_speed", edited_anim, frames->get_animation_speed(edited_anim));
	undo_redo->add_do_method(this, "_update_library", true);
	undo_redo->add_undo_method(this, "_update_library", true);

	undo_redo->commit_action();
}

void SpriteFramesEditor::_tree_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventMouseButton> mb = p_event;

	if (mb.is_valid()) {
		if (mb->get_button_index() == MouseButton::WHEEL_UP && mb->is_pressed() && mb->is_ctrl_pressed()) {
			_zoom_in();
			// Don't scroll up after zooming in.
			accept_event();
		} else if (mb->get_button_index() == MouseButton::WHEEL_DOWN && mb->is_pressed() && mb->is_ctrl_pressed()) {
			_zoom_out();
			// Don't scroll down after zooming out.
			accept_event();
		}
	}
}

void SpriteFramesEditor::_zoom_in() {
	// Do not zoom in or out with no visible frames
	if (frames->get_frame_count(edited_anim) <= 0) {
		return;
	}
	if (thumbnail_zoom < max_thumbnail_zoom) {
		thumbnail_zoom *= scale_ratio;
		int thumbnail_size = (int)(thumbnail_default_size * thumbnail_zoom);
		tree->set_fixed_column_width(thumbnail_size * 3 / 2);
		tree->set_fixed_icon_size(Size2(thumbnail_size, thumbnail_size));
	}
}

void SpriteFramesEditor::_zoom_out() {
	// Do not zoom in or out with no visible frames
	if (frames->get_frame_count(edited_anim) <= 0) {
		return;
	}
	if (thumbnail_zoom > min_thumbnail_zoom) {
		thumbnail_zoom /= scale_ratio;
		int thumbnail_size = (int)(thumbnail_default_size * thumbnail_zoom);
		tree->set_fixed_column_width(thumbnail_size * 3 / 2);
		tree->set_fixed_icon_size(Size2(thumbnail_size, thumbnail_size));
	}
}

void SpriteFramesEditor::_zoom_reset() {
	thumbnail_zoom = MAX(1.0f, EDSCALE);
	tree->set_fixed_column_width(thumbnail_default_size * 3 / 2);
	tree->set_fixed_icon_size(Size2(thumbnail_default_size, thumbnail_default_size));
}

void SpriteFramesEditor::_update_library(bool p_skip_selector) {
	updating = true;

	if (!p_skip_selector) {
		animations->clear();

		TreeItem *anim_root = animations->create_item();

		List<StringName> anim_names;
		frames->get_animation_list(&anim_names);
		anim_names.sort_custom<StringName::AlphCompare>();

		bool searching = anim_search_box->get_text().size();
		String searched_string = searching ? anim_search_box->get_text().to_lower() : String();

		for (const StringName &E : anim_names) {
			String name = E;

			if (searching && name.to_lower().find(searched_string) < 0) {
				continue;
			}

			TreeItem *it = animations->create_item(anim_root);

			it->set_metadata(0, name);

			it->set_text(0, name);
			it->set_editable(0, true);

			if (E == edited_anim) {
				it->select(0);
			}
		}
	}

	tree->clear();

	if (!frames->has_animation(edited_anim)) {
		updating = false;
		return;
	}

	if (sel >= frames->get_frame_count(edited_anim)) {
		sel = frames->get_frame_count(edited_anim) - 1;
	} else if (sel < 0 && frames->get_frame_count(edited_anim)) {
		sel = 0;
	}

	for (int i = 0; i < frames->get_frame_count(edited_anim); i++) {
		String name;
		Ref<Texture2D> frame = frames->get_frame(edited_anim, i);

		if (frame.is_null()) {
			name = itos(i) + ": " + TTR("(empty)");
		} else {
			name = itos(i) + ": " + frame->get_name();
		}

		tree->add_item(name, frame);
		if (frame.is_valid()) {
			String tooltip = frame->get_path();

			// Frame is often saved as an AtlasTexture subresource within a scene/resource file,
			// thus its path might be not what the user is looking for. So we're also showing
			// subsequent source texture paths.
			String prefix = String::utf8("┖╴");
			Ref<AtlasTexture> at = frame;
			while (at.is_valid() && at->get_atlas().is_valid()) {
				tooltip += "\n" + prefix + at->get_atlas()->get_path();
				prefix = "    " + prefix;
				at = at->get_atlas();
			}

			tree->set_item_tooltip(-1, tooltip);
		}
		if (sel == i) {
			tree->select(tree->get_item_count() - 1);
		}
	}

	anim_speed->set_value(frames->get_animation_speed(edited_anim));
	anim_loop->set_pressed(frames->get_animation_loop(edited_anim));

	updating = false;
}

void SpriteFramesEditor::edit(SpriteFrames *p_frames) {
	bool new_read_only_state = false;
	if (p_frames) {
		new_read_only_state = EditorNode::get_singleton()->is_resource_read_only(p_frames);
	}

	if (frames == p_frames && new_read_only_state == read_only) {
		return;
	}

	frames = p_frames;
	read_only = new_read_only_state;

	if (p_frames) {
		if (!p_frames->has_animation(edited_anim)) {
			List<StringName> anim_names;
			frames->get_animation_list(&anim_names);
			anim_names.sort_custom<StringName::AlphCompare>();
			if (anim_names.size()) {
				edited_anim = anim_names.front()->get();
			} else {
				edited_anim = StringName();
			}
		}

		_update_library();
		// Clear zoom and split sheet texture
		split_sheet_preview->set_texture(Ref<Texture2D>());
		_zoom_reset();
	} else {
		hide();
	}

	add_anim->set_disabled(read_only);
	delete_anim->set_disabled(read_only);
	anim_speed->set_editable(!read_only);
	anim_loop->set_disabled(read_only);
	load->set_disabled(read_only);
	load_sheet->set_disabled(read_only);
	copy->set_disabled(read_only);
	paste->set_disabled(read_only);
	empty_before->set_disabled(read_only);
	empty_after->set_disabled(read_only);
	move_up->set_disabled(read_only);
	move_down->set_disabled(read_only);
	delete_frame->set_disabled(read_only);
}

Variant SpriteFramesEditor::get_drag_data_fw(const Point2 &p_point, Control *p_from) {
	if (read_only) {
		return false;
	}

	if (!frames->has_animation(edited_anim)) {
		return false;
	}

	int idx = tree->get_item_at_position(p_point, true);

	if (idx < 0 || idx >= frames->get_frame_count(edited_anim)) {
		return Variant();
	}

	Ref<Resource> frame = frames->get_frame(edited_anim, idx);

	if (frame.is_null()) {
		return Variant();
	}

	Dictionary drag_data = EditorNode::get_singleton()->drag_resource(frame, p_from);
	drag_data["frame"] = idx; // store the frame, in case we want to reorder frames inside 'drop_data_fw'
	return drag_data;
}

bool SpriteFramesEditor::can_drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) const {
	if (read_only) {
		return false;
	}

	Dictionary d = p_data;

	if (!d.has("type")) {
		return false;
	}

	// reordering frames
	if (d.has("from") && (Object *)(d["from"]) == tree) {
		return true;
	}

	if (String(d["type"]) == "resource" && d.has("resource")) {
		Ref<Resource> r = d["resource"];

		Ref<Texture2D> texture = r;

		if (texture.is_valid()) {
			return true;
		}
	}

	if (String(d["type"]) == "files") {
		Vector<String> files = d["files"];

		if (files.size() == 0) {
			return false;
		}

		for (int i = 0; i < files.size(); i++) {
			String f = files[i];
			String ftype = EditorFileSystem::get_singleton()->get_file_type(f);

			if (!ClassDB::is_parent_class(ftype, "Texture2D")) {
				return false;
			}
		}

		return true;
	}
	return false;
}

void SpriteFramesEditor::drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) {
	if (!can_drop_data_fw(p_point, p_data, p_from)) {
		return;
	}

	Dictionary d = p_data;

	if (!d.has("type")) {
		return;
	}

	int at_pos = tree->get_item_at_position(p_point, true);

	if (String(d["type"]) == "resource" && d.has("resource")) {
		Ref<Resource> r = d["resource"];

		Ref<Texture2D> texture = r;

		if (texture.is_valid()) {
			bool reorder = false;
			if (d.has("from") && (Object *)(d["from"]) == tree) {
				reorder = true;
			}

			Ref<EditorUndoRedoManager> &undo_redo = EditorNode::get_undo_redo();
			if (reorder) { //drop is from reordering frames
				int from_frame = -1;
				if (d.has("frame")) {
					from_frame = d["frame"];
				}

				undo_redo->create_action(TTR("Move Frame"));
				undo_redo->add_do_method(frames, "remove_frame", edited_anim, from_frame == -1 ? frames->get_frame_count(edited_anim) : from_frame);
				undo_redo->add_do_method(frames, "add_frame", edited_anim, texture, at_pos == -1 ? -1 : at_pos);
				undo_redo->add_undo_method(frames, "remove_frame", edited_anim, at_pos == -1 ? frames->get_frame_count(edited_anim) - 1 : at_pos);
				undo_redo->add_undo_method(frames, "add_frame", edited_anim, texture, from_frame);
				undo_redo->add_do_method(this, "_update_library");
				undo_redo->add_undo_method(this, "_update_library");
				undo_redo->commit_action();
			} else {
				undo_redo->create_action(TTR("Add Frame"));
				undo_redo->add_do_method(frames, "add_frame", edited_anim, texture, at_pos == -1 ? -1 : at_pos);
				undo_redo->add_undo_method(frames, "remove_frame", edited_anim, at_pos == -1 ? frames->get_frame_count(edited_anim) : at_pos);
				undo_redo->add_do_method(this, "_update_library");
				undo_redo->add_undo_method(this, "_update_library");
				undo_redo->commit_action();
			}
		}
	}

	if (String(d["type"]) == "files") {
		Vector<String> files = d["files"];

		if (Input::get_singleton()->is_key_pressed(Key::CTRL)) {
			_prepare_sprite_sheet(files[0]);
		} else {
			_file_load_request(files, at_pos);
		}
	}
}

void SpriteFramesEditor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_update_library", "skipsel"), &SpriteFramesEditor::_update_library, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("_get_drag_data_fw"), &SpriteFramesEditor::get_drag_data_fw);
	ClassDB::bind_method(D_METHOD("_can_drop_data_fw"), &SpriteFramesEditor::can_drop_data_fw);
	ClassDB::bind_method(D_METHOD("_drop_data_fw"), &SpriteFramesEditor::drop_data_fw);
}

SpriteFramesEditor::SpriteFramesEditor() {
	VBoxContainer *vbc_animlist = memnew(VBoxContainer);
	add_child(vbc_animlist);
	vbc_animlist->set_custom_minimum_size(Size2(150, 0) * EDSCALE);

	VBoxContainer *sub_vb = memnew(VBoxContainer);
	vbc_animlist->add_margin_child(TTR("Animations:"), sub_vb, true);
	sub_vb->set_v_size_flags(SIZE_EXPAND_FILL);

	HBoxContainer *hbc_animlist = memnew(HBoxContainer);
	sub_vb->add_child(hbc_animlist);

	add_anim = memnew(Button);
	add_anim->set_flat(true);
	hbc_animlist->add_child(add_anim);
	add_anim->connect("pressed", callable_mp(this, &SpriteFramesEditor::_animation_add));

	delete_anim = memnew(Button);
	delete_anim->set_flat(true);
	hbc_animlist->add_child(delete_anim);
	delete_anim->set_disabled(true);
	delete_anim->connect("pressed", callable_mp(this, &SpriteFramesEditor::_animation_remove));

	anim_search_box = memnew(LineEdit);
	hbc_animlist->add_child(anim_search_box);
	anim_search_box->set_h_size_flags(SIZE_EXPAND_FILL);
	anim_search_box->set_placeholder(TTR("Filter Animations"));
	anim_search_box->set_clear_button_enabled(true);
	anim_search_box->connect("text_changed", callable_mp(this, &SpriteFramesEditor::_animation_search_text_changed));

	animations = memnew(Tree);
	sub_vb->add_child(animations);
	animations->set_v_size_flags(SIZE_EXPAND_FILL);
	animations->set_hide_root(true);
	animations->connect("cell_selected", callable_mp(this, &SpriteFramesEditor::_animation_select));
	animations->connect("item_edited", callable_mp(this, &SpriteFramesEditor::_animation_name_edited));
	animations->set_allow_reselect(true);

	add_anim->set_shortcut_context(animations);
	add_anim->set_shortcut(ED_SHORTCUT("sprite_frames/new_animation", TTR("Add Animation"), KeyModifierMask::CMD_OR_CTRL | Key::N));
	delete_anim->set_shortcut_context(animations);
	delete_anim->set_shortcut(ED_SHORTCUT("sprite_frames/delete_animation", TTR("Delete Animation"), Key::KEY_DELETE));

	HBoxContainer *hbc_anim_speed = memnew(HBoxContainer);
	hbc_anim_speed->add_child(memnew(Label(TTR("Speed:"))));
	vbc_animlist->add_child(hbc_anim_speed);
	anim_speed = memnew(SpinBox);
	anim_speed->set_suffix(TTR("FPS"));
	anim_speed->set_min(0);
	anim_speed->set_max(100);
	anim_speed->set_step(0.01);
	anim_speed->set_h_size_flags(SIZE_EXPAND_FILL);
	hbc_anim_speed->add_child(anim_speed);
	anim_speed->connect("value_changed", callable_mp(this, &SpriteFramesEditor::_animation_fps_changed));

	anim_loop = memnew(CheckButton);
	anim_loop->set_text(TTR("Loop"));
	vbc_animlist->add_child(anim_loop);
	anim_loop->connect("pressed", callable_mp(this, &SpriteFramesEditor::_animation_loop_changed));

	VBoxContainer *vbc = memnew(VBoxContainer);
	add_child(vbc);
	vbc->set_h_size_flags(SIZE_EXPAND_FILL);

	sub_vb = memnew(VBoxContainer);
	vbc->add_margin_child(TTR("Animation Frames:"), sub_vb, true);

	HBoxContainer *hbc = memnew(HBoxContainer);
	sub_vb->add_child(hbc);

	load = memnew(Button);
	load->set_flat(true);
	hbc->add_child(load);

	load_sheet = memnew(Button);
	load_sheet->set_flat(true);
	hbc->add_child(load_sheet);

	hbc->add_child(memnew(VSeparator));

	copy = memnew(Button);
	copy->set_flat(true);
	hbc->add_child(copy);

	paste = memnew(Button);
	paste->set_flat(true);
	hbc->add_child(paste);

	hbc->add_child(memnew(VSeparator));

	empty_before = memnew(Button);
	empty_before->set_flat(true);
	hbc->add_child(empty_before);

	empty_after = memnew(Button);
	empty_after->set_flat(true);
	hbc->add_child(empty_after);

	hbc->add_child(memnew(VSeparator));

	move_up = memnew(Button);
	move_up->set_flat(true);
	hbc->add_child(move_up);

	move_down = memnew(Button);
	move_down->set_flat(true);
	hbc->add_child(move_down);

	delete_frame = memnew(Button);
	delete_frame->set_flat(true);
	hbc->add_child(delete_frame);

	hbc->add_spacer();

	zoom_out = memnew(Button);
	zoom_out->connect("pressed", callable_mp(this, &SpriteFramesEditor::_zoom_out));
	zoom_out->set_flat(true);
	zoom_out->set_tooltip_text(TTR("Zoom Out"));
	hbc->add_child(zoom_out);

	zoom_reset = memnew(Button);
	zoom_reset->connect("pressed", callable_mp(this, &SpriteFramesEditor::_zoom_reset));
	zoom_reset->set_flat(true);
	zoom_reset->set_tooltip_text(TTR("Zoom Reset"));
	hbc->add_child(zoom_reset);

	zoom_in = memnew(Button);
	zoom_in->connect("pressed", callable_mp(this, &SpriteFramesEditor::_zoom_in));
	zoom_in->set_flat(true);
	zoom_in->set_tooltip_text(TTR("Zoom In"));
	hbc->add_child(zoom_in);

	file = memnew(EditorFileDialog);
	add_child(file);

	tree = memnew(ItemList);
	tree->set_v_size_flags(SIZE_EXPAND_FILL);
	tree->set_icon_mode(ItemList::ICON_MODE_TOP);

	tree->set_max_columns(0);
	tree->set_icon_mode(ItemList::ICON_MODE_TOP);
	tree->set_max_text_lines(2);
	tree->set_drag_forwarding(this);
	tree->connect("gui_input", callable_mp(this, &SpriteFramesEditor::_tree_input));

	sub_vb->add_child(tree);

	dialog = memnew(AcceptDialog);
	add_child(dialog);

	load->connect("pressed", callable_mp(this, &SpriteFramesEditor::_load_pressed));
	load_sheet->connect("pressed", callable_mp(this, &SpriteFramesEditor::_open_sprite_sheet));
	delete_frame->connect("pressed", callable_mp(this, &SpriteFramesEditor::_delete_pressed));
	copy->connect("pressed", callable_mp(this, &SpriteFramesEditor::_copy_pressed));
	paste->connect("pressed", callable_mp(this, &SpriteFramesEditor::_paste_pressed));
	empty_before->connect("pressed", callable_mp(this, &SpriteFramesEditor::_empty_pressed));
	empty_after->connect("pressed", callable_mp(this, &SpriteFramesEditor::_empty2_pressed));
	move_up->connect("pressed", callable_mp(this, &SpriteFramesEditor::_up_pressed));
	move_down->connect("pressed", callable_mp(this, &SpriteFramesEditor::_down_pressed));

	load->set_shortcut_context(tree);
	load->set_shortcut(ED_SHORTCUT("sprite_frames/load_from_file", TTR("Add frame from file"), KeyModifierMask::CMD_OR_CTRL | Key::O));
	load_sheet->set_shortcut_context(tree);
	load_sheet->set_shortcut(ED_SHORTCUT("sprite_frames/load_from_sheet", TTR("Add frames from sprite sheet"), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::O));
	delete_frame->set_shortcut_context(tree);
	delete_frame->set_shortcut(ED_SHORTCUT("sprite_frames/delete", TTR("Delete Frame"), Key::KEY_DELETE));
	copy->set_shortcut_context(tree);
	copy->set_shortcut(ED_SHORTCUT("sprite_frames/copy", TTR("Copy Frame"), KeyModifierMask::CMD_OR_CTRL | Key::C));
	paste->set_shortcut_context(tree);
	paste->set_shortcut(ED_SHORTCUT("sprite_frames/paste", TTR("Paste Frame"), KeyModifierMask::CMD_OR_CTRL | Key::V));
	empty_before->set_shortcut_context(tree);
	empty_before->set_shortcut(ED_SHORTCUT("sprite_frames/empty_before", TTR("Insert Empty (Before Selected)"), KeyModifierMask::ALT | Key::LEFT));
	empty_after->set_shortcut_context(tree);
	empty_after->set_shortcut(ED_SHORTCUT("sprite_frames/empty_after", TTR("Insert Empty (After Selected)"), KeyModifierMask::ALT | Key::RIGHT));
	move_up->set_shortcut_context(tree);
	move_up->set_shortcut(ED_SHORTCUT("sprite_frames/move_left", TTR("Move Frame Left"), KeyModifierMask::CMD_OR_CTRL | Key::LEFT));
	move_down->set_shortcut_context(tree);
	move_down->set_shortcut(ED_SHORTCUT("sprite_frames/move_right", TTR("Move Frame Right"), KeyModifierMask::CMD_OR_CTRL | Key::RIGHT));

	zoom_out->set_shortcut_context(tree);
	zoom_out->set_shortcut(ED_SHORTCUT_ARRAY("sprite_frames/zoom_out", TTR("Zoom Out"),
			{ int32_t(KeyModifierMask::CMD_OR_CTRL | Key::MINUS), int32_t(KeyModifierMask::CMD_OR_CTRL | Key::KP_SUBTRACT) }));
	zoom_in->set_shortcut_context(tree);
	zoom_in->set_shortcut(ED_SHORTCUT_ARRAY("sprite_frames/zoom_in", TTR("Zoom In"),
			{ int32_t(KeyModifierMask::CMD_OR_CTRL | Key::EQUAL), int32_t(KeyModifierMask::CMD_OR_CTRL | Key::KP_ADD) }));

	file->connect("files_selected", callable_mp(this, &SpriteFramesEditor::_file_load_request).bind(-1));
	loading_scene = false;
	sel = -1;

	updating = false;

	edited_anim = "default";

	delete_dialog = memnew(ConfirmationDialog);
	add_child(delete_dialog);
	delete_dialog->connect("confirmed", callable_mp(this, &SpriteFramesEditor::_animation_remove_confirmed));

	split_sheet_dialog = memnew(ConfirmationDialog);
	add_child(split_sheet_dialog);
	VBoxContainer *split_sheet_vb = memnew(VBoxContainer);
	split_sheet_dialog->add_child(split_sheet_vb);
	split_sheet_dialog->set_title(TTR("Select Frames"));
	split_sheet_dialog->connect("confirmed", callable_mp(this, &SpriteFramesEditor::_sheet_add_frames));

	HBoxContainer *split_sheet_hb = memnew(HBoxContainer);

	split_sheet_hb->add_child(memnew(Label(TTR("Horizontal:"))));
	split_sheet_h = memnew(SpinBox);
	split_sheet_h->set_min(1);
	split_sheet_h->set_max(128);
	split_sheet_h->set_step(1);
	split_sheet_hb->add_child(split_sheet_h);
	split_sheet_h->connect("value_changed", callable_mp(this, &SpriteFramesEditor::_sheet_spin_changed).bind(PARAM_FRAME_COUNT));

	split_sheet_hb->add_child(memnew(Label(TTR("Vertical:"))));
	split_sheet_v = memnew(SpinBox);
	split_sheet_v->set_min(1);
	split_sheet_v->set_max(128);
	split_sheet_v->set_step(1);
	split_sheet_hb->add_child(split_sheet_v);
	split_sheet_v->connect("value_changed", callable_mp(this, &SpriteFramesEditor::_sheet_spin_changed).bind(PARAM_FRAME_COUNT));

	split_sheet_hb->add_child(memnew(VSeparator));
	split_sheet_hb->add_child(memnew(Label(TTR("Size:"))));
	split_sheet_size_x = memnew(SpinBox);
	split_sheet_size_x->set_min(1);
	split_sheet_size_x->set_step(1);
	split_sheet_size_x->set_suffix("px");
	split_sheet_size_x->connect("value_changed", callable_mp(this, &SpriteFramesEditor::_sheet_spin_changed).bind(PARAM_SIZE));
	split_sheet_hb->add_child(split_sheet_size_x);
	split_sheet_size_y = memnew(SpinBox);
	split_sheet_size_y->set_min(1);
	split_sheet_size_y->set_step(1);
	split_sheet_size_y->set_suffix("px");
	split_sheet_size_y->connect("value_changed", callable_mp(this, &SpriteFramesEditor::_sheet_spin_changed).bind(PARAM_SIZE));
	split_sheet_hb->add_child(split_sheet_size_y);

	split_sheet_hb->add_child(memnew(VSeparator));
	split_sheet_hb->add_child(memnew(Label(TTR("Separation:"))));
	split_sheet_sep_x = memnew(SpinBox);
	split_sheet_sep_x->set_min(0);
	split_sheet_sep_x->set_step(1);
	split_sheet_sep_x->set_suffix("px");
	split_sheet_sep_x->connect("value_changed", callable_mp(this, &SpriteFramesEditor::_sheet_spin_changed).bind(PARAM_USE_CURRENT));
	split_sheet_hb->add_child(split_sheet_sep_x);
	split_sheet_sep_y = memnew(SpinBox);
	split_sheet_sep_y->set_min(0);
	split_sheet_sep_y->set_step(1);
	split_sheet_sep_y->set_suffix("px");
	split_sheet_sep_y->connect("value_changed", callable_mp(this, &SpriteFramesEditor::_sheet_spin_changed).bind(PARAM_USE_CURRENT));
	split_sheet_hb->add_child(split_sheet_sep_y);

	split_sheet_hb->add_child(memnew(VSeparator));
	split_sheet_hb->add_child(memnew(Label(TTR("Offset:"))));
	split_sheet_offset_x = memnew(SpinBox);
	split_sheet_offset_x->set_min(0);
	split_sheet_offset_x->set_step(1);
	split_sheet_offset_x->set_suffix("px");
	split_sheet_offset_x->connect("value_changed", callable_mp(this, &SpriteFramesEditor::_sheet_spin_changed).bind(PARAM_USE_CURRENT));
	split_sheet_hb->add_child(split_sheet_offset_x);
	split_sheet_offset_y = memnew(SpinBox);
	split_sheet_offset_y->set_min(0);
	split_sheet_offset_y->set_step(1);
	split_sheet_offset_y->set_suffix("px");
	split_sheet_offset_y->connect("value_changed", callable_mp(this, &SpriteFramesEditor::_sheet_spin_changed).bind(PARAM_USE_CURRENT));
	split_sheet_hb->add_child(split_sheet_offset_y);

	split_sheet_hb->add_spacer();

	Button *select_clear_all = memnew(Button);
	select_clear_all->set_text(TTR("Select/Clear All Frames"));
	select_clear_all->connect("pressed", callable_mp(this, &SpriteFramesEditor::_sheet_select_clear_all_frames));
	split_sheet_hb->add_child(select_clear_all);

	split_sheet_vb->add_child(split_sheet_hb);

	PanelContainer *split_sheet_panel = memnew(PanelContainer);
	split_sheet_panel->set_h_size_flags(SIZE_EXPAND_FILL);
	split_sheet_panel->set_v_size_flags(SIZE_EXPAND_FILL);
	split_sheet_vb->add_child(split_sheet_panel);

	split_sheet_preview = memnew(TextureRect);
	split_sheet_preview->set_ignore_texture_size(true);
	split_sheet_preview->set_mouse_filter(MOUSE_FILTER_PASS);
	split_sheet_preview->connect("draw", callable_mp(this, &SpriteFramesEditor::_sheet_preview_draw));
	split_sheet_preview->connect("gui_input", callable_mp(this, &SpriteFramesEditor::_sheet_preview_input));

	split_sheet_scroll = memnew(ScrollContainer);
	split_sheet_scroll->connect("gui_input", callable_mp(this, &SpriteFramesEditor::_sheet_scroll_input));
	split_sheet_panel->add_child(split_sheet_scroll);
	CenterContainer *cc = memnew(CenterContainer);
	cc->add_child(split_sheet_preview);
	cc->set_h_size_flags(SIZE_EXPAND_FILL);
	cc->set_v_size_flags(SIZE_EXPAND_FILL);
	split_sheet_scroll->add_child(cc);

	MarginContainer *split_sheet_zoom_margin = memnew(MarginContainer);
	split_sheet_panel->add_child(split_sheet_zoom_margin);
	split_sheet_zoom_margin->set_h_size_flags(0);
	split_sheet_zoom_margin->set_v_size_flags(0);
	split_sheet_zoom_margin->add_theme_constant_override("margin_top", 5);
	split_sheet_zoom_margin->add_theme_constant_override("margin_left", 5);
	HBoxContainer *split_sheet_zoom_hb = memnew(HBoxContainer);
	split_sheet_zoom_margin->add_child(split_sheet_zoom_hb);

	split_sheet_zoom_out = memnew(Button);
	split_sheet_zoom_out->set_flat(true);
	split_sheet_zoom_out->set_focus_mode(FOCUS_NONE);
	split_sheet_zoom_out->set_tooltip_text(TTR("Zoom Out"));
	split_sheet_zoom_out->connect("pressed", callable_mp(this, &SpriteFramesEditor::_sheet_zoom_out));
	split_sheet_zoom_hb->add_child(split_sheet_zoom_out);

	split_sheet_zoom_reset = memnew(Button);
	split_sheet_zoom_reset->set_flat(true);
	split_sheet_zoom_reset->set_focus_mode(FOCUS_NONE);
	split_sheet_zoom_reset->set_tooltip_text(TTR("Zoom Reset"));
	split_sheet_zoom_reset->connect("pressed", callable_mp(this, &SpriteFramesEditor::_sheet_zoom_reset));
	split_sheet_zoom_hb->add_child(split_sheet_zoom_reset);

	split_sheet_zoom_in = memnew(Button);
	split_sheet_zoom_in->set_flat(true);
	split_sheet_zoom_in->set_focus_mode(FOCUS_NONE);
	split_sheet_zoom_in->set_tooltip_text(TTR("Zoom In"));
	split_sheet_zoom_in->connect("pressed", callable_mp(this, &SpriteFramesEditor::_sheet_zoom_in));
	split_sheet_zoom_hb->add_child(split_sheet_zoom_in);

	file_split_sheet = memnew(EditorFileDialog);
	file_split_sheet->set_title(TTR("Create Frames from Sprite Sheet"));
	file_split_sheet->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	add_child(file_split_sheet);
	file_split_sheet->connect("file_selected", callable_mp(this, &SpriteFramesEditor::_prepare_sprite_sheet));

	// Config scale.
	scale_ratio = 1.2f;
	thumbnail_default_size = 96 * MAX(1, EDSCALE);
	thumbnail_zoom = MAX(1.0f, EDSCALE);
	max_thumbnail_zoom = 8.0f * MAX(1.0f, EDSCALE);
	min_thumbnail_zoom = 0.1f * MAX(1.0f, EDSCALE);
	// Default the zoom to match the editor scale, but don't dezoom on editor scales below 100% to prevent pixel art from looking bad.
	sheet_zoom = MAX(1.0f, EDSCALE);
	max_sheet_zoom = 16.0f * MAX(1.0f, EDSCALE);
	min_sheet_zoom = 0.01f * MAX(1.0f, EDSCALE);
	_zoom_reset();

	// Ensure the anim search box is wide enough by default.
	// Not by setting its minimum size so it can still be shrunk if desired.
	set_split_offset(56 * EDSCALE);
}

void SpriteFramesEditorPlugin::edit(Object *p_object) {
	SpriteFrames *s;
	AnimatedSprite2D *animated_sprite = Object::cast_to<AnimatedSprite2D>(p_object);
	if (animated_sprite) {
		s = *animated_sprite->get_sprite_frames();
	} else {
		AnimatedSprite3D *animated_sprite_3d = Object::cast_to<AnimatedSprite3D>(p_object);
		if (animated_sprite_3d) {
			s = *animated_sprite_3d->get_sprite_frames();
		} else {
			s = Object::cast_to<SpriteFrames>(p_object);
		}
	}

	frames_editor->edit(s);
}

bool SpriteFramesEditorPlugin::handles(Object *p_object) const {
	AnimatedSprite2D *animated_sprite = Object::cast_to<AnimatedSprite2D>(p_object);
	AnimatedSprite3D *animated_sprite_3d = Object::cast_to<AnimatedSprite3D>(p_object);
	if (animated_sprite && *animated_sprite->get_sprite_frames()) {
		return true;
	} else if (animated_sprite_3d && *animated_sprite_3d->get_sprite_frames()) {
		return true;
	} else {
		return p_object->is_class("SpriteFrames");
	}
}

void SpriteFramesEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		button->show();
		EditorNode::get_singleton()->make_bottom_panel_item_visible(frames_editor);
	} else {
		button->hide();
		if (frames_editor->is_visible_in_tree()) {
			EditorNode::get_singleton()->hide_bottom_panel();
		}
	}
}

SpriteFramesEditorPlugin::SpriteFramesEditorPlugin() {
	frames_editor = memnew(SpriteFramesEditor);
	frames_editor->set_custom_minimum_size(Size2(0, 300) * EDSCALE);
	button = EditorNode::get_singleton()->add_bottom_panel_item(TTR("SpriteFrames"), frames_editor);
	button->hide();
}

SpriteFramesEditorPlugin::~SpriteFramesEditorPlugin() {
}
