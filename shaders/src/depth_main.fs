#if defined(HAS_VSM)
layout(location = 0) out vec4 fragColor;
#endif

//------------------------------------------------------------------------------
// Depth
//------------------------------------------------------------------------------

#if defined(HAS_TRANSPARENT_SHADOW)
float bayer2x2(highp vec2 p) {
    return mod(2.0 * p.y + p.x + 1.0, 4.0);
}

float bayer8x8(highp vec2 p) {
    vec2 p1 = mod(p, 2.0);
    vec2 p2 = floor(0.5  * mod(p, 4.0));
    vec2 p4 = floor(0.25 * mod(p, 8.0));
    return 4.0 * (4.0 * bayer2x2(p1) + bayer2x2(p2)) + bayer2x2(p4);
}
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
    if ((bayer8x8(floor(mod(gl_FragCoord.xy, 8.0)))) / 64.0 >= alpha) {
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
