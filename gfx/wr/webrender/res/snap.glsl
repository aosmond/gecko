/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef WR_VERTEX_SHADER

/// Given a point within a local rectangle, and the device space corners
/// offsets for the unsnapped primitive, return the snap offsets. This *must*
/// exactly match the logic in the Rust compute_snap_offset_impl function.
vec2 compute_snap_offset(
    vec2 reference_pos,
    RectWithSize reference_rect,
    vec4 snap_positions
) {
    /// Compute the position of this vertex inside the snap rectangle.
    vec2 normalized_snap_pos = (reference_pos - reference_rect.p0) / reference_rect.size;

    /// Compute the actual world offset for this vertex needed to make it snap.
    return mix(snap_positions.xy, snap_positions.zw, normalized_snap_pos);
}

RectWithSize compute_snapped_rect(
    RectWithSize reference_rect,
    mat4 transform,
    float device_pixel_scale,
    vec4 snap_positions
) {
    vec4 p0 = transform * vec4(reference_rect.p0, 0.0, 1.0);
    vec4 p1 = transform * vec4(reference_rect.p0 + reference_rect.size, 0.0, 1.0);
    vec4 pos = device_pixel_scale * vec4(p0.xy, p1.xy) / vec4(p0.ww, p1.ww);
    pos = (pos + snap_positions) / device_pixel_scale;
    mat4 inv_transform = inverse(transform);
    p0 = inv_transform * vec4(pos.xy, 0.0, 1.0);
    p1 = inv_transform * vec4(pos.wz, 0.0, 1.0);
    pos = vec4(p0.xy, p1.xy) / vec4(p0.ww, p1.ww);
    RectWithSize result;
    result.p0 = pos.xy;
    result.size = pos.wz - pos.xy;
    return result;
}

#endif //WR_VERTEX_SHADER
