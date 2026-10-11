////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    quaternions.hlsl
////
////    quaternion support functions
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

#ifndef __QUATERNIONS_HLSL_INCLUDED__
#define __QUATERNIONS_HLSL_INCLUDED__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    includes
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "config.hlsl"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    quaternion inversion functions
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//  conjugate a quaternion
float4 qconj( in float4 q )
{
    return( float4( q.xyz, -q.w ) );
}

//  invert a quaternion
float4 qinv( in float4 q )
{
    return( qconj( q ) * rcp( dot( q, q ) ) );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    quaternion multiplication functions
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//  multiply a quaternion by a second quaternion
float4 qqmul( in float4 q1, in float4 q2 )
{
    return( float4( ( cross( q2.xyz, q1.xyz ) + ( q1.xyz * q2.w ) + ( q2.xyz * q1.w ) ), ( ( q1.w * q2.w ) - dot( q1.xyz, q2.xyz ) ) ) );
}

//  multiply a quaternion by the conjugate of a second quaternion
float4 qcmul( in float4 q1, in float4 q2 )
{
    return( float4( ( cross( q1.xyz, q2.xyz ) + ( q1.xyz * q2.w ) - ( q2.xyz * q1.w ) ), dot( q1, q2 ) ) );
}

//  multiply the conjugate of a quaternion by a second quaternion
float4 cqmul( in float4 q1, in float4 q2 )
{
    return( float4( ( cross( q1.xyz, q2.xyz ) - ( q1.xyz * q2.w ) + ( q2.xyz * q1.w ) ), dot( q1, q2 ) ) );
}

//  multiply the conjugate of a quaternion by the conjugate of a second quaternion
float4 ccmul( in float4 q1, in float4 q2 )
{
    return( float4( ( cross( q2.xyz, q1.xyz ) - ( q1.xyz * q2.w ) - ( q2.xyz * q1.w ) ), ( ( q1.w * q2.w ) - dot( q1.xyz, q2.xyz ) ) ) );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    quaternion axis extraction functions
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//  get the identity frame x axis rotated by the given quaternion assuming that the quaternion is unit length
float3 fastGetQuatAxisX( in float4 q )
{
    return( float3( ( 0.5f - ( q.y * q.y ) - ( q.z * q.z ) ), ( ( q.x * q.y ) + ( q.w * q.z ) ), ( ( q.x * q.z ) - ( q.w * q.y ) ) ) * 2.0f );
}

//  get the identity frame y axis rotated by the given quaternion assuming that the quaternion is unit length
float3 fastGetQuatAxisY( in float4 q )
{
    return( float3( ( ( q.y * q.x ) - ( q.w * q.z ) ), ( 0.5f - ( q.z * q.z ) - ( q.x * q.x ) ), ( ( q.y * q.z ) + ( q.w * q.x ) ) ) * 2.0f );
}

//  get the identity frame z axis rotated by the given quaternion assuming that the quaternion is unit length
float3 fastGetQuatAxisZ( in float4 q )
{
    return( float3( ( ( q.z * q.x ) + ( q.w * q.y ) ), ( ( q.z * q.y ) - ( q.w * q.x ) ), ( 0.5f - ( q.x * q.x ) - ( q.y * q.y ) ) ) * 2.0f );
}

//  get the identity frame rotated by the given quaternion assuming that the quaternion is unit length
void fastGetQuatAxes( in float4 q, out float3 x, out float3 y, out float3 z )
{
    float3 qw2 = ( q.xyz + q.xyz );
    float3 qq2 = ( qw2 * q.xyz );
    float3 qa2 = ( qw2 * q.yzx );
    qw2 *= q.w;
    x = float3( ( 1.0f - qq2.y - qq2.z ), ( qa2.x + qw2.z ), ( qa2.z - qw2.y ) );
    y = float3( ( qa2.x - qw2.z ), ( 1.0f - qq2.z - qq2.x ), ( qa2.y + qw2.x ) );
    z = float3( ( qa2.z + qw2.y ), ( qa2.y - qw2.x ), ( 1.0f - qq2.x - qq2.y ) );
}

//  get the identity frame x axis rotated and scaled by the given quaternion
float3 getQuatAxisX( in float4 q )
{
    float s = dot( q, q );
    float2 yz = ( q.yz + q.yz );
    return( float3( ( s - ( q.y * yz.x ) - ( q.z * yz.y ) ), ( ( q.x * yz.x ) + ( q.w * yz.y ) ), ( ( q.x * yz.y ) - ( q.w * yz.x ) ) ) );
}

//  get the identity frame y axis rotated and scaled by the given quaternion
float3 getQuatAxisY( in float4 q )
{
    float s = dot( q, q );
    float2 zx = ( q.zx + q.zx );
    return( float3( ( ( q.y * zx.y ) - ( q.w * zx.x ) ), ( s - ( q.z * zx.x ) - ( q.x * zx.y ) ), ( ( q.y * zx.x ) + ( q.w * zx.y ) ) ) );
}

//  get the identity frame z axis rotated and scaled by the given quaternion
float3 getQuatAxisZ( in float4 q )
{
    float s = dot( q, q );
    float2 xy = ( q.xy + q.xy );
    return( float3( ( ( q.z * xy.x ) + ( q.w * xy.y ) ), ( ( q.z * xy.y ) - ( q.w * xy.x ) ), ( s - ( q.x * xy.x ) - ( q.y * xy.y ) ) ) );
}

//  get the identity frame rotated and scaled by the given quaternion
void getQuatAxes( in float4 q, out float3 x, out float3 y, out float3 z )
{
    float s = dot( q, q );
    float3 qw2 = ( q.xyz + q.xyz );
    float3 qq2 = ( qw2 * q.xyz );
    float3 qa2 = ( qw2 * q.yzx );
    qw2 *= q.w;
    x = float3( ( s - qq2.y - qq2.z ), ( qa2.x + qw2.z ), ( qa2.z - qw2.y ) );
    y = float3( ( qa2.x - qw2.z ), ( s - qq2.z - qq2.x ), ( qa2.y + qw2.x ) );
    z = float3( ( qa2.z + qw2.y ), ( qa2.y - qw2.x ), ( s - qq2.x - qq2.y ) );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    vector rotation functions
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//  rotate a vector by the quaternion assuming that the quaternion is unit length
float3 fastRotateVector( in float3 v, in float4 q )
{
    float3 t = cross( q.xyz, v );
    t += t;
    return( v + cross( q.xyz, t ) + ( t * q.w ) );
}

//  rotate a vector by the inverse of the quaternion assuming that the quaternion is unit length
float3 fastInverseRotateVector( in float3 v, in float4 q )
{
    float3 t = cross( q.xyz, v );
    t += t;
    return( v + cross( q.xyz, t ) - ( t * q.w ) );
}

//  rotate a vector by the quaternion
float3 rotateVector( in float3 v, in float4 q )
{
    float s = dot( q, q );
    float3 t = cross( q.xyz, v );
    t += t;
    return( v + ( ( cross( q.xyz, t ) + ( t * q.w ) ) * rcp( s ) ) );
}

//  rotate a vector by the inverse of the quaternion
float3 inverseRotateVector( in float3 v, in float4 q )
{
    float s = dot( q, q );
    float3 t = cross( q.xyz, v );
    t += t;
    return( v + ( ( cross( q.xyz, t ) - ( t * q.w ) ) * rcp( s ) ) );
}

//  rotate and scale a vector by the quaternion
float3 transformVector( in float3 v, in float4 q )
{
    float s = dot( q, q );
    float3 t = cross( q.xyz, v );
    t += t;
    return( ( v * s ) + cross( q.xyz, t ) + ( t * q.w ) );
}

//  rotate and scale a vector by the inverse of the quaternion
float3 inverseTransformVector( in float3 v, in float4 q )
{
    float s = dot( q, q );
    float3 t = cross( q.xyz, v );
    t += t;
    return( ( ( v * s ) + cross( q.xyz, t ) - ( t * q.w ) ) * rcp( s * s ) );
}

//  rotate and scale a vector by the conjugate of the quaternion
float3 conjugateTransformVector( in float3 v, in float4 q )
{
    float s = dot( q, q );
    float3 t = cross( q.xyz, v );
    t += t;
    return( ( v * s ) + cross( q.xyz, t ) - ( t * q.w ) );
}

//  rotate and scale a vector by the inverse conjugate of the quaternion
float3 inverseConjugateTransformVector( in float3 v, in float4 q )
{
    float s = dot( q, q );
    float3 t = cross( q.xyz, v );
    t += t;
    return( ( ( v * s ) + cross( q.xyz, t ) + ( t * q.w ) ) * rcp( s * s ) );
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    quaternion tangent-space functions
////
////    notes:
////
////        Quaternion tangent space is a unit quaternion with the handedness of the space encoded as an
////        offset to the w (real) component.
////
////        Standard tangent space is encoded by adding 2 to the w component.
////        Inverse tangent space is encoded by subtracting 2 from the w component.
////
////        Inverse tangent space has its bitangent (y axis) negated relative to standard tangent space.
////
////        The tangent space axes are:
////            x => tangent
////            y => bitangent
////            z => normal
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//  rotate tspace by the quaternion assuming that the quaternion is unit length
float4 fastRotateTSpace( in float4 tspace, in float4 q )
{
    float4 t = tspace;
    float handedness = ( sign( t.w ) * 2 );
    t.w -= handedness;
    t = qqmul( t, q );
    t.w += handedness;
    return( t );
}

//  rotate tspace by the inverse of the quaternion assuming that the quaternion is unit length
float4 fastInverseRotateTSpace( in float4 tspace, in float4 q )
{
    float4 t = tspace;
    float handedness = ( sign( t.w ) * 2 );
    t.w -= handedness;
    t = qcmul( t, q );
    t.w += handedness;
    return( t );
}

//  rotate tspace by the quaternion
float4 rotateTSpace( in float4 tspace, in float4 q )
{
    float4 t = tspace;
    float handedness = ( sign( t.w ) * 2 );
    t.w -= handedness;
    t = normalize( qqmul( t, q ) );
    t.w += handedness;
    return( t );
}

//  rotate tspace by the inverse of the quaternion
float4 inverseRotateTSpace( in float4 tspace, in float4 q )
{
    float4 t = tspace;
    float handedness = ( sign( t.w ) * 2 );
    t.w -= handedness;
    t = normalize( qcmul( t, q ) );
    t.w += handedness;
    return( t );
}

//  extract the normal vector from a quaternion encoded tangent-space
float3 getTSpaceNormal( in float4 tspace )
{
    float4 t = tspace;
    float handedness = sign( t.w );
    t.w -= ( handedness + handedness );
    return( fastGetQuatAxisZ( normalize( t ) ) );
}

//  extract the axis vectors from a quaternion encoded tangent-space
void getTSpaceAxes( in float4 tspace, out float3 tangent, out float3 bitangent, out float3 normal )
{
    float4 t = tspace;
    float handedness = sign( t.w );
    t.w -= ( handedness + handedness );
    fastGetQuatAxes( normalize( t ), tangent, bitangent, normal );
    bitangent *= handedness;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    end of include guard
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif  //  #ifndef __QUATERNIONS_HLSL_INCLUDED__

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////
////    end of file
////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
