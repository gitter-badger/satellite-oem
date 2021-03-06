#ifndef _VERTEX_H
#define _VERTEX_H

#include "../include/mathlib.glsl"


// for geometry accumulators
#ifdef VERTEX_FILLING
layout ( std430, binding = 0, set = 0 ) restrict buffer BuildCounters { int tcounter[1]; };
layout ( std430, binding = 1, set = 0 ) restrict buffer GeomMaterialsSSBO { int materials[]; };
layout ( std430, binding = 2, set = 0 ) restrict buffer OrderIdxSSBO { int vorders[]; };
layout ( std430, binding = 3, set = 0 ) restrict buffer VertexLinearSSBO { float lvtx[]; };
layout ( rgba32f, binding = 4, set = 0 ) uniform image2D attrib_texture_out;
#else

// for traverse, or anything else
#ifndef BVH_CREATION
layout ( std430, binding = 0, set = 1 ) readonly buffer BVHBoxBlock { uvec4 bvhBoxes[][4]; }; 
layout ( binding = 5, set = 1 ) uniform isampler2D bvhStorage;
#endif

layout ( binding = 10, set = 1 ) uniform sampler2D attrib_texture;
layout ( std430, binding = 1, set = 1 ) readonly buffer GeomMaterialsSSBO { int materials[]; };
layout ( std430, binding = 2, set = 1 ) readonly buffer OrderIdxSSBO { int vorders[]; };
layout ( std430, binding = 3, set = 1 ) readonly buffer GeometryBlockUniform { GeometryUniformStruct geometryUniform;} geometryBlock;
layout ( std430, binding = 7, set = 1 ) readonly buffer VertexLinearSSBO { float lvtx[]; };
#endif

const int ATTRIB_EXTENT = 4;

// attribute formating
const int NORMAL_TID = 0;
const int TEXCOORD_TID = 1;
const int COLOR_TID = 2; // unused
const int MODF_TID = 3; // at now no supported 

#ifdef ENABLE_AMD_INSTRUCTION_SET
#define ISTORE(img, crd, data) imageStoreLodAMD(img, crd, 0, data)
#define SGATHER(smp, crd, chnl) textureGatherLodAMD(smp, crd, 0, chnl)
#else
#define ISTORE(img, crd, data) imageStore(img, crd, data)
#define SGATHER(smp, crd, chnl) textureGather(smp, crd, chnl)
#endif

//#define _SWIZV wzx
#define _SWIZV xyz

const int WARPED_WIDTH = 2048;
//const ivec2 mit[3] = {ivec2(0,0), ivec2(1,0), ivec2(0,1)};
const ivec2 mit[3] = {ivec2(0,1), ivec2(1,1), ivec2(1,0)};

ivec2 mosaicIdc(in ivec2 mosaicCoord, in int idc) {
#ifdef VERTEX_FILLING
    mosaicCoord.x %= int(imageSize(attrib_texture_out).x);
#endif
    return mosaicCoord + mit[idc];
}

ivec2 gatherMosaic(in ivec2 uniformCoord) {
    return ivec2(uniformCoord.x * 3 + uniformCoord.y % 3, uniformCoord.y);
}

vec4 fetchMosaic(in sampler2D vertices, in ivec2 mosaicCoord, in uint idc) {
    //return texelFetch(vertices, mosaicCoord + mit[idc], 0);
    return textureLod(vertices, (vec2(mosaicCoord + mit[idc]) + 0.49999f) / textureSize(vertices, 0), 0); // supper native warping
}

ivec2 getUniformCoord(in int indice) {
    return ivec2(indice % WARPED_WIDTH, indice / WARPED_WIDTH);
}

ivec2 getUniformCoord(in uint indice) {
    return ivec2(indice % WARPED_WIDTH, indice / WARPED_WIDTH);
}


#ifndef VERTEX_FILLING
float intersectTriangle(in vec3 orig, in mat3 M, in int axis, in int tri, inout vec2 UV, inout BOOL_ _valid, in float testdist) {
    float T = INFINITY;
    //IFANY (_valid) {
        BOOL_ valid = tri < 0 ? FALSE_ : _valid; // pre-define
        const vec3 D[3] = {vec3(1.f, 0.f, 0.f), vec3(0.f, 1.f, 0.f), vec3(0.f, 0.f, 1.f)};
        //const vec2 sz = 1.f / textureSize(vertex_texture, 0), hs = sz * 0.9999f;
        IFANY (valid) {
            // gather patterns
            const int itri = tri*9;
            const mat3 ABC = transpose(mat3(
                vec3(lvtx[itri+0], lvtx[itri+1], lvtx[itri+2])-orig,
                vec3(lvtx[itri+3], lvtx[itri+4], lvtx[itri+5])-orig,
                vec3(lvtx[itri+6], lvtx[itri+7], lvtx[itri+8])-orig
            ))*M;

            // PURE watertight triangle intersection (our, GPU-GLSL adapted version)
            // http://jcgt.org/published/0002/01/05/paper.pdf
            vec3 UVW_ = D[axis] * inverse(ABC);
            valid &= BOOL_(all(greaterThanEqual(UVW_, vec3(0.f))) || all(lessThanEqual(UVW_, vec3(0.f))));
            IFANY (valid) {
                float det = dot(UVW_,vec3(1)); UVW_ *= 1.f/(max(abs(det),0.00001f)*(det>=0.f?1:-1));
                UV = vec2(UVW_.yz), UVW_ *= ABC; // calculate axis distances
                T = mix(mix(UVW_.z, UVW_.y, axis == 1), UVW_.x, axis == 0);
                T = mix(INFINITY, T, greaterEqualF(T, 0.0f) & valid);
            }
        }
    //}
    return T;
}
#endif



const int _BVH_WIDTH = 2048;

ivec2 bvhLinear2D(in int linear) {
    int md = linear & 1; linear >>= 1;
    return ivec2(linear % _BVH_WIDTH, ((linear / _BVH_WIDTH) << 1) + md);
}

#ifndef BVH_CREATION
#ifndef VERTEX_FILLING
vec2 bvhGatherifyStorage(in ivec2 ipt){
    const vec2 sz = 1.f / textureSize(bvhStorage, 0), hs = sz * 0.9999f;
    return fma(vec2(ipt), sz, hs);
}
#endif
#endif

#endif
