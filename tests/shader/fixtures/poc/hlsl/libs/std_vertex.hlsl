////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    std_vertex.hlsl
////
////    standard vertex structures and transform functions
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    start of include guard
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __STD_VERTEX_HLSL_INCLUDED__
#define __STD_VERTEX_HLSL_INCLUDED__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    configuration notes:
////
////        ( VTX_ENABLE_ZBIAS == ENABLED )
////
////            applies the entity translation.w component as a depth bias
////            obviously this is only applied if there is an entity
////
////        ( VTX_ENABLE_INTPOS == ENABLED )
////
////            makes the .position vertex element integer rather than the default float
////            enabling this define allows integer types to be used for positions
////
////            detail: the input assembler handles short, ushort, int and uint types assigned to
////            float registers with the equivalent of a reinterpret_cast rather than a static_cast.
////
////        ( VTX_ENABLE_DRAW2D == ENABLED )
////
////            makes the .position vertex element 2D rather than the default 3D
////
////        ( VTX_ENABLE_RELATIVE == ENABLED )
////
////            adds the .relative element to the interpolator structure
////            outputs the camera relative world-space oriented vertex position to the interpolators
////
////        ( VTX_ENABLE_VIEWPOS == ENABLED )
////
////            adds the .viewpos element to the interpolator structure
////            outputs the view-space transformed vertex position to the interpolators
////
////        ( VTX_ENABLE_LOCALPOS == ENABLED )
////
////            adds the .localpos element to the interpolator structure
////            outputs the un-transformed vertex position to the interpolators
////
////        ( VTX_ENABLE_VNORMAL == ENABLED )
////
////            adds the .normal element to the vertex and interpolator structures
////            outputs the vertex normal transformed by the entity transform (if any) and the camera transform to the interpolators
////
////        ( VTX_ENABLE_NORMAL == ENABLED )
////
////            ignored if ( VTX_ENABLE_VNORMAL == ENABLED )
////            adds the .normal element to the vertex and interpolator structures
////            outputs the vertex normal transformed by the entity transform (if any) to the interpolators
////
////        ( VTX_ENABLE_VTSPACE == ENABLED )
////
////            adds the .tspace element to the vertex and interpolator structures
////            outputs the vertex tspace transformed by the entity transform (if any) and the camera transform to the interpolators
////
////        ( VTX_ENABLE_TSPACE == ENABLED )
////
////            ignored if ( VTX_ENABLE_VTSPACE == ENABLED )
////            adds the .tspace element to the vertex and interpolator structures
////            outputs the vertex tspace transformed by the entity transform (if any) to the interpolators
////
////        ( VTX_ENABLE_COLOUR == ENABLED )
////
////            adds the .colour element to the vertex and interpolator structures
////            passes the colour element through from the vertex data to the interpolators
////
////        ( VTX_ENABLE_UVUV == ENABLED )
////
////            adds the .uvuv element to the vertex and interpolator structures
////            passes the double texture coordinate element through from the vertex data to the interpolators
////
////        ( VTX_ENABLE_UV == ENABLED )
////
////            ignored if ( VTX_ENABLE_UVUV == ENABLED )
////            adds the .uv element to the vertex and interpolator structures
////            passes the single texture coordinate element through from the vertex data to the interpolators
////
////        ( VTX_ENABLE_INSTANCE == ENABLED )
////
////            adds the .instance element to the vertex structure with standard semantics
////
////        ( VTX_ENABLE_SUBMODEL == ENABLED )
////
////            adds the .submodel element to the vertex structure
////            for use with multi-part models which index different entitys
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    includes
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "vertex_utils.hlsl"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    disable undefined configurations
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef VTX_ENABLE_ZBIAS
#define VTX_ENABLE_ZBIAS    DISABLED
#endif

#ifndef VTX_ENABLE_INTPOS
#define VTX_ENABLE_INTPOS   DISABLED
#endif

#ifndef VTX_ENABLE_DRAW2D
#define VTX_ENABLE_DRAW2D   DISABLED
#endif

#ifndef VTX_ENABLE_INSTANCE
#define VTX_ENABLE_INSTANCE DISABLED
#endif

#ifndef VTX_ENABLE_SUBMODEL
#define VTX_ENABLE_SUBMODEL DISABLED
#endif

#ifndef VTX_ENABLE_SKINNING
#define VTX_ENABLE_SKINNING DISABLED
#endif

#ifndef VTX_ENABLE_RELATIVE
#define VTX_ENABLE_RELATIVE DISABLED
#endif

#ifndef VTX_ENABLE_VIEWPOS
#define VTX_ENABLE_VIEWPOS  DISABLED
#endif

#ifndef VTX_ENABLE_LOCALPOS
#define VTX_ENABLE_LOCALPOS  DISABLED
#endif

#ifndef VTX_ENABLE_VNORMAL
#define VTX_ENABLE_VNORMAL  DISABLED
#endif

#ifndef VTX_ENABLE_NORMAL
#define VTX_ENABLE_NORMAL   DISABLED
#endif

#ifndef VTX_ENABLE_VTSPACE
#define VTX_ENABLE_VTSPACE  DISABLED
#endif

#ifndef VTX_ENABLE_TSPACE
#define VTX_ENABLE_TSPACE   DISABLED
#endif

#ifndef VTX_ENABLE_COLOUR
#define VTX_ENABLE_COLOUR   DISABLED
#endif

#ifndef VTX_ENABLE_UVUV
#define VTX_ENABLE_UVUV     DISABLED
#endif

#ifndef VTX_ENABLE_UV
#define VTX_ENABLE_UV       DISABLED
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    standard vertex input structure
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Vertex
{
#if ( VTX_ENABLE_INTPOS == ENABLED )
#if ( VTX_ENABLE_DRAW2D == ENABLED )
    int2        position        :   POSITION;
#else
    int3        position        :   POSITION;
#endif
#else
#if ( VTX_ENABLE_DRAW2D == ENABLED )
    float2      position        :   POSITION;
#else
    float3      position        :   POSITION;
#endif
#endif

#if ( ( VTX_ENABLE_NORMAL == ENABLED ) || ( VTX_ENABLE_VNORMAL == ENABLED ) )
    float3      normal          :   NORMAL;
#endif

#if ( ( VTX_ENABLE_TSPACE == ENABLED ) || ( VTX_ENABLE_VTSPACE == ENABLED ) )
    float4      tspace          :   TSPACE;
#endif

#if ( VTX_ENABLE_UVUV == ENABLED )
    float4      uvuv            :   UVUV;
#endif

#if ( VTX_ENABLE_UV == ENABLED )
    float2      uv              :   UV;
#endif

#if ( VTX_ENABLE_COLOUR == ENABLED )
    float4      colour          :   COLOUR;
#endif

#if ( VTX_ENABLE_SUBMODEL == ENABLED )
    uint        submodel        :   SUBMODEL;
#endif

#if ( VTX_ENABLE_SKINNING == ENABLED )
    uint4       indices         :   BLENDINDICES;
    float4      weights         :   BLENDWEIGHTS;
#endif

#if ( VTX_ENABLE_INSTANCE == ENABLED )
    uint        instance        :   SV_InstanceID;
#endif
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    standard vertex output structure
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Interpolators
{
    float4      hclip           :   SV_Position;

#if ( VTX_ENABLE_RELATIVE == ENABLED )
    float3      relative        :   RELATIVE;
#endif

#if ( VTX_ENABLE_VIEWPOS == ENABLED )
    float3      viewpos         :   VIEWPOS;
#endif

#if ( VTX_ENABLE_LOCALPOS == ENABLED )
#if ( VTX_ENABLE_DRAW2D == ENABLED )
    float2      localpos        :   LOCALPOS;
#else
    float3      localpos        :   LOCALPOS;
#endif
#endif

#if ( ( VTX_ENABLE_NORMAL == ENABLED ) || ( VTX_ENABLE_VNORMAL == ENABLED ) )
    float3      normal          :   NORMAL;
#endif

#if ( ( VTX_ENABLE_TSPACE == ENABLED ) || ( VTX_ENABLE_VTSPACE == ENABLED ) )
    float4      tspace          :   TSPACE;
#endif

#if ( VTX_ENABLE_COLOUR == ENABLED )
    float4      colour          :   COLOUR;
#endif

#if ( VTX_ENABLE_UVUV == ENABLED )
    float4      uvuv            :   UVUV;
#endif

#if ( ( VTX_ENABLE_UV == ENABLED ) && ( VTX_ENABLE_UVUV != ENABLED ) )
    float2      uv              :   UV;
#endif
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    standard vertex transformation functions
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//  transform a simple world-space vertex
void std_transformVertex( in Camera camera, in Vertex vertex, out Interpolators interpolators )
{
#if ( VTX_ENABLE_DRAW2D == ENABLED )
    float3 position = float3( vertex.position.xy, 0.0f );
#else
    float3 position = vertex.position.xyz;
#endif

    float3 relative = getInterpolatorRelative( camera, position );
#if ( VTX_ENABLE_RELATIVE == ENABLED )
    interpolators.relative = relative;
#endif

    float3 viewpos = getInterpolatorViewPos( camera, relative );
#if ( VTX_ENABLE_VIEWPOS == ENABLED )
    interpolators.viewpos = viewpos;
#endif

    interpolators.hclip = getInterpolatorHClip( camera, viewpos );

#if ( VTX_ENABLE_LOCALPOS == ENABLED )
    interpolators.localpos = vertex.position;
#endif

#if ( VTX_ENABLE_VNORMAL == ENABLED )
    interpolators.normal = getInterpolatorNormal( camera, vertex.normal );
#elif ( VTX_ENABLE_NORMAL == ENABLED )
    interpolators.normal = getInterpolatorNormal( vertex.normal );
#endif

#if ( VTX_ENABLE_VTSPACE == ENABLED )
    interpolators.tspace = getInterpolatorTSpace( camera, vertex.tspace );
#elif ( VTX_ENABLE_TSPACE == ENABLED )
    interpolators.tspace = getInterpolatorTSpace( vertex.tspace );
#endif

#if ( VTX_ENABLE_COLOUR == ENABLED )
    interpolators.colour = vertex.colour;
#endif

#if ( VTX_ENABLE_UVUV == ENABLED )
    interpolators.uvuv = vertex.uvuv;
#endif

#if ( VTX_ENABLE_UV == ENABLED ) && ( VTX_ENABLE_UVUV != ENABLED )
    interpolators.uv = vertex.uv;
#endif
}

//  transform an entitys local-space vertex
void std_transformVertex( in Camera camera, in Entity entity, in Vertex vertex, out Interpolators interpolators )
{
#if ( VTX_ENABLE_DRAW2D == ENABLED )
    float3 position = float3( vertex.position.xy, 0.0f );
#else
    float3 position = vertex.position.xyz;
#endif

    float3 relative = getInterpolatorRelative( camera, entity, position );
#if ( VTX_ENABLE_RELATIVE == ENABLED )
    interpolators.relative = relative;
#endif

    float3 viewpos = getInterpolatorViewPos( camera, relative );
#if ( VTX_ENABLE_VIEWPOS == ENABLED )
    interpolators.viewpos = viewpos;
#endif

    interpolators.hclip = getInterpolatorHClip( camera, viewpos );
#if ( VTX_ENABLE_ZBIAS == ENABLED )
    interpolators.hclip.z += entity.locate.w;
#endif

#if ( VTX_ENABLE_LOCALPOS == ENABLED )
    interpolators.localpos = vertex.position;
#endif

#if ( VTX_ENABLE_VNORMAL == ENABLED )
    interpolators.normal = getInterpolatorNormal( camera, entity, vertex.normal );
#elif ( VTX_ENABLE_NORMAL == ENABLED )
    interpolators.normal = getInterpolatorNormal( entity, vertex.normal );
#endif

#if ( VTX_ENABLE_VTSPACE == ENABLED )
    interpolators.tspace = getInterpolatorTSpace( camera, entity, vertex.tspace );
#elif ( VTX_ENABLE_TSPACE == ENABLED )
    interpolators.tspace = getInterpolatorTSpace( entity, vertex.tspace );
#endif

#if ( VTX_ENABLE_COLOUR == ENABLED )
    interpolators.colour = vertex.colour;
#endif

#if ( VTX_ENABLE_UVUV == ENABLED )
    interpolators.uvuv = vertex.uvuv;
#endif

#if ( VTX_ENABLE_UV == ENABLED ) && ( VTX_ENABLE_UVUV != ENABLED )
    interpolators.uv = vertex.uv;
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    end of include guard
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif  //  #ifndef __STD_VERTEX_HLSL_INCLUDED__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    end of file
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
