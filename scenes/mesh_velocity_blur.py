#!/usr/bin/env python

# 1 point cloud with 1 dome light with an HDRI
# Copyright (c) 2011-2017 Hiroshi Tsubokawa

# NOTE How to make dragon_vel.mesh
# $ ply2mesh dragon.ply  dragon.mesh
# $ velgen   dragon.mesh dragon_vel.mesh

import fujiyama

si = fujiyama.SceneInterface()

#plugins
si.OpenPlugin('constant_shader', 'ConstantShader')
si.OpenPlugin('plastic_shader', 'PlasticShader')
si.OpenPlugin('velocity_generator_procedure', 'VelocityGeneratorProcedure')
si.OpenPlugin('stanfordply_procedure', 'StanfordPlyProcedure')

#Camera
si.NewCamera('cam1', 'PerspectiveCamera')
si.SetProperty3('cam1', 'translate', 0, 1, 5)
#si.SetProperty3('cam1', 'rotate', -1, 0, 0)

#Light
rot = -30
rot = 120
si.NewLight('light1', 'DomeLight')
si.SetProperty3('light1', 'rotate', 0, rot, 0)
si.SetProperty1('light1', 'sample_count', 32)

#Texture
si.NewTexture('tex1', '../../hdr/grossglockner02.hdr')
si.AssignTexture('light1', 'environment_map', 'tex1');

#Shader
si.NewShader('floor_shader', 'plastic_shader')
si.SetProperty3('floor_shader', 'diffuse', .2, .25, .3)

si.NewShader('dragon_shader', 'plastic_shader')
si.SetProperty3('dragon_shader', 'reflect', .0, .0, .0)

si.NewShader('dome_shader', 'constant_shader')
si.AssignTexture('dome_shader', 'texture', 'tex1')

#Mesh
si.NewMesh('dragon_mesh', 'null')
si.NewMesh('floor_mesh',  'null')
si.NewMesh('dome_mesh',   'null')

#Procedure
si.NewProcedure('dragon_proc', 'stanfordply_procedure')
si.AssignMesh('dragon_proc', 'mesh', 'dragon_mesh')
si.SetStringProperty('dragon_proc', 'filepath', '../../ply/dragon.ply')
si.SetStringProperty('dragon_proc', 'io_mode', 'r')
si.RunProcedure('dragon_proc')

si.NewProcedure('floor_proc', 'stanfordply_procedure')
si.AssignMesh('floor_proc', 'mesh', 'floor_mesh')
si.SetStringProperty('floor_proc', 'filepath', '../../ply/floor.ply')
si.SetStringProperty('floor_proc', 'io_mode', 'r')
si.RunProcedure('floor_proc')

si.NewProcedure('dome_proc', 'stanfordply_procedure')
si.AssignMesh('dome_proc', 'mesh', 'dome_mesh')
si.SetStringProperty('dome_proc', 'filepath', '../../ply/dome.ply')
si.SetStringProperty('dome_proc', 'io_mode', 'r')
si.RunProcedure('dome_proc')

si.NewProcedure('velgen_proc', 'velocity_generator_procedure')
si.AssignMesh('velgen_proc', 'mesh', 'dragon_mesh')
si.RunProcedure('velgen_proc')

#ObjectInstance
si.NewObjectInstance('dragon1', 'dragon_mesh')
si.SetProperty3('dragon1', 'rotate', 0, -90, 0)
si.SetProperty3('dragon1', 'scale', .5, .5, .5)
si.AssignShader('dragon1', 'DEFAULT_SHADING_GROUP', 'dragon_shader')

si.NewObjectInstance('floor1', 'floor_mesh')
si.AssignShader('floor1', 'DEFAULT_SHADING_GROUP', 'floor_shader')

si.NewObjectInstance('dome1', 'dome_mesh')
si.AssignShader('dome1', 'DEFAULT_SHADING_GROUP', 'dome_shader')
si.SetProperty3('dome1', 'rotate', 0, rot, 0)

#ObjectGroup
# Create shadow_target for some objects.
# Since 'DomeLight' has infinite distance, we need to exclude
# 'dome1' object which is for just background image.
si.NewObjectGroup('group1')
si.AddObjectToGroup('group1', 'dragon1')
si.AssignObjectGroup('dragon1', 'shadow_target', 'group1')
si.AssignObjectGroup('floor1', 'shadow_target', 'group1')

#FrameBuffer
si.NewFrameBuffer('fb1', 'rgba')

#Renderer
si.NewRenderer('ren1')
si.AssignCamera('ren1', 'cam1')
si.AssignFrameBuffer('ren1', 'fb1')
si.SetProperty2('ren1', 'resolution', 640, 480)
#si.SetProperty2('ren1', 'resolution', 160, 120)

# pixelsamples specifies how many rays will be shot within a pixel.
# The default value is 3 by 3. This scene uses 6 by 6 to get
# better motion blur.
si.SetProperty2('ren1', 'pixelsamples', 6, 6)

#Rendering
si.RenderScene('ren1')

#Output
si.SaveFrameBuffer('fb1', '../mesh_velocity_blur.fb')

#Run commands
si.Run()
#si.Print()
