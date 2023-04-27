/*
    SPDX-FileCopyrightText: 2008, 2011 Martin Gräßlin <kde@martin-graesslin.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
uniform mat4 modelViewProjectionMatrix;
uniform float width;
uniform float cubeAngle;
uniform float xCoord;
uniform float timeLine;

attribute vec4 position;
attribute vec4 texcoord;

varying vec2 texcoord0;

void main()
{
    texcoord0 = texcoord.st;
    vec4 transformedVertex = vec4(position.x - ( width - xCoord ), position.yzw);
    float radian = radians(cubeAngle);
    float radius = (width)*tan(radian);
    float azimuthAngle = radians(transformedVertex.x/(width)*(90.0 - cubeAngle));

    transformedVertex.x = width - xCoord + radius * sin( azimuthAngle );
    transformedVertex.z = position.z + radius * cos( azimuthAngle ) - radius;

    vec3 diff = (position.xyz - transformedVertex.xyz)*timeLine;
    transformedVertex.xyz += diff;

    gl_Position = modelViewProjectionMatrix*transformedVertex;
}
