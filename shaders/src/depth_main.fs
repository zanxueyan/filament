#if defined(HAS_VSM)
layout(location = 0) out vec4 fragColor;
#endif

//------------------------------------------------------------------------------
// Depth
//------------------------------------------------------------------------------

#if defined(HAS_TRANSPARENT_SHADOW)
const float bayer8x8[64] = float[64](
    0.0000, 0.5000, 0.1250, 0.6250, 0.03125, 0.53125, 0.15625, 0.65625,
    0.7500, 0.2500, 0.8750, 0.3750, 0.78125, 0.28125, 0.90625, 0.40625,
    0.1875, 0.6875, 0.0625, 0.5625, 0.21875, 0.71875, 0.09375, 0.59375,
    0.9375, 0.4375, 0.8125, 0.3125, 0.96875, 0.46875, 0.84375, 0.34375,
    0.0469, 0.5469, 0.1719, 0.6719, 0.01563, 0.51563, 0.14063, 0.64063,
    0.7969, 0.2969, 0.9219, 0.4219, 0.76563, 0.26563, 0.89063, 0.39063,
    0.2344, 0.7344, 0.1094, 0.6094, 0.20313, 0.70313, 0.07813, 0.57813,
    0.9844, 0.4844, 0.8594, 0.3594, 0.95313, 0.45313, 0.82813, 0.32813
);
#endif

void main() {
#if defined(BLEND_MODE_MASKED) || (defined(BLEND_MODE_TRANSPARENT) && defined(HAS_TRANSPARENT_SHADOW))
    MaterialInputs inputs;
    initMaterial(inputs);
    material(inputs);

    float alpha = inputs.baseColor.a;
#if defined(BLEND_MODE_MASKED)
    if (alpha < getMaskThreshold()) {
        discard;
    }
#endif

#if defined(HAS_TRANSPARENT_SHADOW)
    vec2 coords = mod(gl_FragCoord.xy, 8.0);
    if (bayer8x8[int(coords.y * 8.0 + coords.x)] >= alpha) {
        discard;
    }
#endif
#endif

#if defined(HAS_VSM)
    // For VSM, we use the linear light space Z coordinate as the depth metric, which works for both
    // directional and spot lights.
    // We negate it, because we're using a right-handed coordinate system (-Z points forward).
    highp float depth = -mulMat4x4Float3(frameUniforms.viewFromWorldMatrix, vertex_worldPosition).z;

    // Scale by cameraFar to help prevent a floating point overflow below when squaring the depth.
    depth /= abs(frameUniforms.cameraFar);

    highp float dx = dFdx(depth);
    highp float dy = dFdy(depth);

    // Output the first and second depth moments.
    // The first moment is mean depth.
    // The second moment is mean depth squared.
    // These values are retrieved when sampling the shadow map to compute variance.
    highp float bias = 0.25 * (dx * dx + dy * dy);
    fragColor = vec4(depth, depth * depth + bias, 0.0, 0.0);
#endif
}
