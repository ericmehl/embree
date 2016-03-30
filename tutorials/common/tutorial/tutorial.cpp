// ======================================================================== //
// Copyright 2009-2016 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#include "tutorial.h"
#include "scene.h"
#include "tutorial_device.h"
#include "../scenegraph/obj_loader.h"
#include "../scenegraph/xml_loader.h"
#include "../image/image.h"
#include "../transport/transport_host.h"

/* include GLUT for display */
#if defined(__MACOSX__)
#  include <OpenGL/gl.h>
#  include <GLUT/glut.h>
#elif defined(__WIN32__)
#  include <windows.h>
#  include <GL/gl.h>   
#  include <GL/glut.h>
#else
#  include <GL/gl.h>   
#  include <GL/glut.h>
#endif

extern "C" {
  float g_debug = 0.0f;
}

namespace embree
{
  TutorialApplication::TutorialApplication (const std::string& tutorialName)

    : tutorialName(tutorialName),
      rtcore(""),
      numThreads(0),
      subdiv_mode(""),
      width(512), height(512), fullscreen(false),
      outFilename(""),
      skipBenchmarkFrames(0),
      numBenchmarkFrames(0),
      interactive(true),
      instancing_mode(0),
      shader(SHADER_DEFAULT),
      convert_tris_to_quads(false),
      convert_bezier_to_lines(false),
      convert_hair_to_curves(false),
      scene(new SceneGraph::GroupNode),
      filename("")
  {
    /* for best performance set FTZ and DAZ flags in MXCSR control and status register */
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    
    registerOption("help", [this] (Ref<ParseStream> cin, const FileName& path) {
        printCommandLineHelp();
        exit(1);
      }, "--help: prints help for all supported command line options");
    
    registerOption("c", [this] (Ref<ParseStream> cin, const FileName& path) {
        FileName file = path + cin->getFileName();
        parseCommandLine(new ParseStream(new LineCommentFilter(file, "#")), file.path());
      }, "-c <filename>: parses command line option from <filename>");
    
    registerOption("i", [this] (Ref<ParseStream> cin, const FileName& path) {
        filename = path + cin->getFileName();
      }, "-i <filename>: parses scene from <filename>");
    
    registerOption("o", [this] (Ref<ParseStream> cin, const FileName& path) {
        outFilename = cin->getFileName();
        interactive = false;
      }, "-o: output image filename");
    
    registerOption("convert-triangles-to-quads", [this] (Ref<ParseStream> cin, const FileName& path) {
        convert_tris_to_quads = true;
      }, "--convert-triangles-to-quads: converts all triangles to quads when loading");
    
    registerOption("convert-bezier-to-lines", [this] (Ref<ParseStream> cin, const FileName& path) {
        convert_bezier_to_lines = true;
      }, "--convert-bezier-to-lines: converts all bezier curves to line segments when loading");
    
    registerOption("convert-hair-to-curves", [this] (Ref<ParseStream> cin, const FileName& path) {
        convert_hair_to_curves = true;
      }, "--convert-hair-to-curves: converts all hair geometry to curves when loading");
    
    /* camera settings */
    registerOption("vp", [this] (Ref<ParseStream> cin, const FileName& path) {
        g_camera.from = cin->getVec3fa();
      }, "--vp <float> <float> <float>: camera position");
    
    registerOption("vi", [this] (Ref<ParseStream> cin, const FileName& path) {
        g_camera.to = cin->getVec3fa();
      }, "--vi <float> <float> <float>: camera lookat position");
    
    registerOption("vd", [this] (Ref<ParseStream> cin, const FileName& path) {
        g_camera.to = g_camera.from + cin->getVec3fa();
      }, "--vd <float> <float> <float>: camera direction vector");
    
    registerOption("vu", [this] (Ref<ParseStream> cin, const FileName& path) {
        g_camera.up = cin->getVec3fa();
      }, "--vu <float> <float> <float>: camera up vector");
    
    registerOption("fov", [this] (Ref<ParseStream> cin, const FileName& path) {
        g_camera.fov = cin->getFloat();
      }, "--fov <float>: vertical field of view");
    
    /* framebuffer settings */
    registerOption("size", [this] (Ref<ParseStream> cin, const FileName& path) {
        width = cin->getInt();
        height = cin->getInt();
      }, "--size <width> <height>: image size");
    
    registerOption("fullscreen", [this] (Ref<ParseStream> cin, const FileName& path) {
        fullscreen = true;
      }, "--fullscreen: starts in fullscreen mode");
    
    registerOption("rtcore", [this] (Ref<ParseStream> cin, const FileName& path) {
        rtcore += "," + cin->getString();
      }, "--rtcore <string>: uses <string> to configure Embree device");
    
    registerOption("threads", [this] (Ref<ParseStream> cin, const FileName& path) {
        numThreads = cin->getInt();
        rtcore += ",threads=" + toString(numThreads);
      }, "--threads <int>: number of threads to use");
    
    registerOption("benchmark", [this] (Ref<ParseStream> cin, const FileName& path) {
        skipBenchmarkFrames = cin->getInt();
        numBenchmarkFrames  = cin->getInt();
        interactive = false;
        rtcore += ",benchmark=1";
      }, "--benchmark <N> <M>: enabled benchmark mode, skips N frames, renders M frames ");
    
    /* output filename */
    registerOption("shader", [this] (Ref<ParseStream> cin, const FileName& path) {
        std::string mode = cin->getString();
        if      (mode == "default" ) shader = SHADER_DEFAULT;
        else if (mode == "eyelight") shader = SHADER_EYELIGHT;
        else if (mode == "uv"      ) shader = SHADER_UV;
        else if (mode == "Ng"      ) shader = SHADER_NG;
        else if (mode == "geomID"  ) shader = SHADER_GEOMID;
        else if (mode == "primID"  ) shader = SHADER_GEOMID_PRIMID;
        else if (mode == "ao"      ) shader = SHADER_AMBIENT_OCCLUSION;
        else throw std::runtime_error("invalid shader:" +mode);
      }, 
      "--shader <string>: sets shader to use at startup\n"
      "  default: default tutorial shader\n"
      "  eyelight: eyelight shading\n"
      "  uv: uv debug shader\n"
      "  Ng: visualization of shading normal\n"
      "  geomID: visualization of geometry ID\n"
      "  primID: visualization of geometry and primitive ID\n"
      "  ao: ambient occlusion shader");
    
    registerOption("cache", [this] (Ref<ParseStream> cin, const FileName& path) {
        subdiv_mode = ",subdiv_accel=bvh4.subdivpatch1cached";
        rtcore += subdiv_mode;
      }, "--cache: enabled cached subdiv mode");
    
    registerOption("pregenerate", [this] (Ref<ParseStream> cin, const FileName& path) {
        subdiv_mode = ",subdiv_accel=bvh4.grid.eager";
        rtcore += subdiv_mode;
      }, "--pregenerate: enabled pregenerate subdiv mode");
    
    registerOption("instancing", [this] (Ref<ParseStream> cin, const FileName& path) {
        std::string mode = cin->getString();
        if      (mode == "none"    ) instancing_mode = TutorialScene::INSTANCING_NONE;
        //else if (mode == "geometry") instancing_mode = TutorialScene::INSTANCING_GEOMETRY;
        else if (mode == "scene_geometry") instancing_mode = TutorialScene::INSTANCING_SCENE_GEOMETRY;
        else if (mode == "scene_group"   ) instancing_mode = TutorialScene::INSTANCING_SCENE_GROUP;
        else throw std::runtime_error("unknown instancing mode: "+mode);
      }, "--instancing: set instancing mode\n"
      "  none: no instancing\n"
      "  geometry: instance individual geometries\n"
      "  scene_geometry: instance individual geometries as scenes\n"
      "  scene_group: instance geometry groups as scenes\n");
    
    registerOption("ambientlight", [this] (Ref<ParseStream> cin, const FileName& path) {
        const Vec3fa L = cin->getVec3fa();
        scene->add(new SceneGraph::LightNode<AmbientLight>(AmbientLight(L)));
      }, "--ambientlight r g b: adds an ambient light with intensity rgb");
    registerAlternativeOption("ambientlight","ambient");

    registerOption("pointlight", [this] (Ref<ParseStream> cin, const FileName& path) {
        const Vec3fa P = cin->getVec3fa();
        const Vec3fa I = cin->getVec3fa();
        scene->add(new SceneGraph::LightNode<PointLight>(PointLight(P,I)));
      }, "--pointlight x y z r g b: adds a point light at position xyz with intensity rgb");
    
    registerOption("directionallight", [this] (Ref<ParseStream> cin, const FileName& path) {
        const Vec3fa D = cin->getVec3fa();
        const Vec3fa E = cin->getVec3fa();
        scene->add(new SceneGraph::LightNode<DirectionalLight>(DirectionalLight(D,E)));
      }, "--directionallight x y z r g b: adds a directional light with direction xyz and intensity rgb");
    registerAlternativeOption("directionallight","dirlight");
    
    registerOption("distantlight", [this] (Ref<ParseStream> cin, const FileName& path) {
        const Vec3fa D = cin->getVec3fa();
        const Vec3fa L = cin->getVec3fa();
        const float halfAngle = cin->getFloat();
        scene->add(new SceneGraph::LightNode<DistantLight>(DistantLight(D,L,halfAngle)));
      }, "--distantlight x y z r g b a: adds a distant light with direction xyz, intensity rgb, and opening angle a");
  }
  
  void TutorialApplication::registerAlternativeOption(const std::string& name, const std::string& alternativeName) {
    commandLineOptionMap[alternativeName] = commandLineOptionMap[name];
  }
  
  void TutorialApplication::parseCommandLine(int argc, char** argv)
  {
    /* create stream for parsing */
    Ref<ParseStream> stream = new ParseStream(new CommandLineStream(argc, argv));
    
    /* parse command line */  
    parseCommandLine(stream, FileName());
    
    /* callback */
    postParseCommandLine();
  }
  
  void TutorialApplication::parseCommandLine(Ref<ParseStream> cin, const FileName& path)
  {
    while (true)
    {
      std::string tag = cin->getString();
      if (tag == "") return;
      
      /* remove - or -- and lookup command line option */
      if (tag.find("-") == 0) tag = tag.substr(1);
      if (tag.find("-") == 0) tag = tag.substr(1);
      auto option = commandLineOptionMap.find(tag);
      
      /* process command line option */
      if (option != commandLineOptionMap.end()) {
        option->second->parse(cin,path);
      }
      
      /* handle unknown command line options */
      else
      {
        std::cerr << "unknown command line parameter: " << tag << " ";
        while (cin->peek() != "" && cin->peek()[0] != '-') std::cerr << cin->getString() << " ";
        std::cerr << std::endl;
      }    
    }
  }

  void TutorialApplication::printCommandLineHelp()
  {
    for (auto c : commandLineOptionList) {
      std::cout << c->description << std::endl;
    }
  }

  void TutorialApplication::renderBenchmark(const FileName& fileName)
  {
    resize(width,height);
    ISPCCamera camera = g_camera.getISPCCamera(width,height);

    double dt = 0.0f;
    size_t numTotalFrames = skipBenchmarkFrames + numBenchmarkFrames;
    for (size_t i=0; i<numTotalFrames; i++) 
    {
      double t0 = getSeconds();
      render(0.0f,camera);
      double t1 = getSeconds();
      std::cout << "frame [" << i << " / " << numTotalFrames << "] ";
      std::cout << 1.0/(t1-t0) << "fps ";
      if (i < skipBenchmarkFrames) std::cout << "(skipped)";
      std::cout << std::endl;
      if (i >= skipBenchmarkFrames) dt += t1-t0;
    }
    std::cout << "frame [" << skipBenchmarkFrames << " - " << numTotalFrames << "] " << std::flush;
    std::cout << double(numBenchmarkFrames)/dt << "fps " << std::endl;
    std::cout << "BENCHMARK_RENDER " << double(numBenchmarkFrames)/dt << std::endl;
  }

  void TutorialApplication::renderToFile(const FileName& fileName)
  {
    resize(width,height);
    ISPCCamera camera = g_camera.getISPCCamera(width,height);
    render(0.0f,camera);
    void* ptr = map();
    Ref<Image> image = new Image4uc(width, height, (Col4uc*)ptr);
    storeImage(image, fileName);
    unmap();
    cleanup();
  }

  
  int TutorialApplication::main(int argc, char** argv) try
  {
    /* parse command line options */
    parseCommandLine(argc,argv);

    /* load scene */
    if (toLowerCase(filename.ext()) == std::string("obj"))
      scene->add(loadOBJ(filename,subdiv_mode != ""));
    else if (filename.ext() != "")
      scene->add(SceneGraph::load(filename));

    /* convert triangles to quads */
    if (convert_tris_to_quads)
      scene->triangles_to_quads();
    
    /* convert bezier to lines */
    if (convert_bezier_to_lines)
      scene->bezier_to_lines();
    
    /* convert hair to curves */
    if (convert_hair_to_curves)
      scene->hair_to_curves();
    
    /* convert model */
    obj_scene.add(scene.dynamicCast<SceneGraph::Node>(),(TutorialScene::InstancingMode)instancing_mode); 
    scene = nullptr;

    /* send model */
    set_scene(&obj_scene);
    
    /* initialize ray tracing core */
    init(rtcore.c_str());
    
    /* set shader mode */
    switch (shader) {
    case SHADER_DEFAULT : break;
    case SHADER_EYELIGHT: key_pressed(GLUT_KEY_F2); break;
    case SHADER_UV      : key_pressed(GLUT_KEY_F4); break;
    case SHADER_NG      : key_pressed(GLUT_KEY_F5); break;
    case SHADER_GEOMID  : key_pressed(GLUT_KEY_F6); break;
    case SHADER_GEOMID_PRIMID: key_pressed(GLUT_KEY_F7); break;
    case SHADER_AMBIENT_OCCLUSION: key_pressed(GLUT_KEY_F11); break;
    };
    
    /* benchmark mode */
    if (numBenchmarkFrames)
      renderBenchmark(outFilename);
    
    /* render to disk */
    if (outFilename.str() != "")
      renderToFile(outFilename);
    
    /* interactive mode */
    if (interactive) {
      initWindowState(argc,argv,tutorialName, width, height, fullscreen);
      glutMainLoop();
    }

    return 0;
  }  
  catch (const std::exception& e) {
    std::cout << "Error: " << e.what() << std::endl;
    return 1;
  }
  catch (...) {
    std::cout << "Error: unknown exception caught." << std::endl;
    return 1;
  }

  static const double g_time0 = getSeconds();

  /* camera */
  Camera g_camera;

  /* output settings */
  static size_t g_width = 512;
  static size_t g_height = 512;
  static bool g_display = true;

  /* fullscreen settings */
  static bool g_fullscreen = false;
  static size_t g_window_width = 512;
  static size_t g_window_height = 512;

  /* ID of created window */
  static int g_window = 0;

  static int g_debug_int0 = 0;
  static int g_debug_int1 = 0;

  /*************************************************************************************************/
  /*                                  Keyboard control                                             */
  /*************************************************************************************************/

  static float g_speed = 1.0f;

  void TutorialApplication::keyboardFunc(unsigned char key, int x, int y)
  {
    /* call tutorial keyboard handler */
    key_pressed(key);

    switch (key)
    {
    case 'f' : 
      if (g_fullscreen) {
        g_fullscreen = false;
        glutReshapeWindow(g_window_width,g_window_height);
      } else {
        g_fullscreen = true;
        g_window_width = g_width;
        g_window_height = g_height;
        glutFullScreen(); 
      }
      break;
    case 'c' : {
      std::cout.precision(10);
      std::cout << "-vp " << g_camera.from.x    << " " << g_camera.from.y    << " " << g_camera.from.z    << " " 
                << "-vi " << g_camera.to.x << " " << g_camera.to.y << " " << g_camera.to.z << " " 
        //<< "-vd " << g_camera.to.x-g_camera.from.x << " " << g_camera.to.y-g_camera.from.y << " " << g_camera.to.z-g_camera.from.z << " " << std::endl
                << "-vu " << g_camera.up.x     << " " << g_camera.up.y     << " " << g_camera.up.z     << " " 
                << "-fov " << g_camera.fov << std::endl;
      break;
    }
      //case 'a' : g_camera.rotate(-0.02f,0.0f); break;
      //case 'd' : g_camera.rotate(+0.02f,0.0f); break;
      //case 'w' : g_camera.move(0.0f,0.0f,+g_speed); break;
      //case 's' : g_camera.move(0.0f,0.0f,-g_speed); break;
      //case ' ' : g_display = !g_display; break;

    case '+' : g_debug=clamp(g_debug+0.01f); PRINT(g_debug); break;
    case '-' : g_debug=clamp(g_debug-0.01f); PRINT(g_debug); break;

    case '\033': case 'q': case 'Q':
      cleanup();
      glutDestroyWindow(g_window);
#if defined(__MACOSX__)
      exit(1);
#endif
      break;
    }
  }

  void TutorialApplication::specialFunc(int key, int, int)
  {
    key_pressed(key);

    switch (key) {
      //case GLUT_KEY_LEFT      : g_camera.rotate(-0.02f,0.0f); break;
      //case GLUT_KEY_RIGHT     : g_camera.rotate(+0.02f,0.0f); break;
      //case GLUT_KEY_UP        : g_camera.move(0.0f,0.0f,+g_speed); break;
      //case GLUT_KEY_DOWN      : g_camera.move(0.0f,0.0f,-g_speed); break;
    case GLUT_KEY_UP        : g_debug_int0++; set_parameter(1000000,g_debug_int0); PRINT(g_debug_int0); break;
    case GLUT_KEY_DOWN      : g_debug_int0--; set_parameter(1000000,g_debug_int0); PRINT(g_debug_int0); break;
    case GLUT_KEY_LEFT      : g_debug_int1--; set_parameter(1000001,g_debug_int1); PRINT(g_debug_int1); break;
    case GLUT_KEY_RIGHT     : g_debug_int1++; set_parameter(1000001,g_debug_int1); PRINT(g_debug_int1); break;
    case GLUT_KEY_PAGE_UP   : g_speed *= 1.2f; break;
    case GLUT_KEY_PAGE_DOWN : g_speed /= 1.2f; break;
    }
  }

  /*************************************************************************************************/
  /*                                   Mouse control                                               */
  /*************************************************************************************************/

  static int mouseMode = 0;
  static int clickX = 0, clickY = 0;
  static bool flip14 = false;

  void TutorialApplication::clickFunc(int button, int state, int x, int y) 
  {
    if (state == GLUT_UP) 
    {
      mouseMode = 0;
      if (button == GLUT_LEFT_BUTTON && glutGetModifiers() == GLUT_ACTIVE_SHIFT) 
      {
        ISPCCamera camera = g_camera.getISPCCamera(g_width,g_height);
        Vec3fa p; bool hit = pick(x,y,camera,p);

        if (hit) {
          Vec3fa delta = p - g_camera.to;
          Vec3fa right = normalize(camera.xfm.l.vx);
          Vec3fa up    = normalize(camera.xfm.l.vy);
          g_camera.to = p;
          g_camera.from += dot(delta,right)*right + dot(delta,up)*up;
        }
      }
      else if (button == GLUT_LEFT_BUTTON && glutGetModifiers() == (GLUT_ACTIVE_CTRL | GLUT_ACTIVE_SHIFT)) 
      {
        ISPCCamera camera = g_camera.getISPCCamera(g_width,g_height);
        Vec3fa p; bool hit = pick(x,y,camera,p);
        if (hit) g_camera.to = p;
      }

    } else {
      clickX = x; clickY = y;
      int modifiers = glutGetModifiers();
      if      (button == GLUT_LEFT_BUTTON && modifiers == GLUT_ACTIVE_SHIFT) mouseMode = 1;
      else if (button == GLUT_MIDDLE_BUTTON) mouseMode = 2;
      else if (button == GLUT_RIGHT_BUTTON ) mouseMode = 3;
      else if (button == GLUT_LEFT_BUTTON && modifiers == GLUT_ACTIVE_CTRL ) mouseMode = 3;
      else if (button == GLUT_LEFT_BUTTON  ) mouseMode = 4;

      if (flip14) {
        if (mouseMode == 4) mouseMode = 1;
        else if (mouseMode == 1) mouseMode = 4;
      }
    }
  }
  
  void TutorialApplication::motionFunc(int x, int y)
  {
    float dClickX = float(clickX - x), dClickY = float(clickY - y);
    clickX = x; clickY = y;

    switch (mouseMode) {
    case 1: g_camera.rotateOrbit(-0.005f*dClickX,0.005f*dClickY); break;
    case 2: break;
    case 3: g_camera.dolly(-dClickY); break;
    case 4: g_camera.rotate(-0.005f*dClickX,0.005f*dClickY); break;
    }
  }
 
  /*************************************************************************************************/
  /*                                   Window control                                              */
  /*************************************************************************************************/

  void TutorialApplication::displayFunc(void) 
  {
    ISPCCamera camera = g_camera.getISPCCamera(g_width,g_height,true);
    
    /* render image using ISPC */
    double t0 = getSeconds();
    render(g_time0-t0,camera);
    double dt0 = getSeconds()-t0;

    if (g_display) 
    {
      /* draw pixels to screen */
      int* pixels = map();
      glDrawPixels(g_width,g_height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);

      if (g_fullscreen) 
      {
        glMatrixMode( GL_PROJECTION );
        glPushMatrix();
        glLoadIdentity();
        gluOrtho2D( 0, g_width, 0, g_height );
        glMatrixMode( GL_MODELVIEW );
        glPushMatrix();
        glLoadIdentity();

         /* print frame rate */
        std::ostringstream stream;
        stream.setf(std::ios::fixed, std::ios::floatfield);
        stream.precision(2);
        stream << 1.0f/dt0 << " fps";
        std::string str = stream.str();

        glRasterPos2i( g_width-str.size()*12, g_height - 24); 
        for ( int i = 0; i < str.size(); ++i )
          glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, str[i]);
        
        glRasterPos2i( 0, 0 ); 
        glPopMatrix();
        glMatrixMode( GL_PROJECTION );
        glPopMatrix();
        glMatrixMode( GL_MODELVIEW );
      }
      
      glutSwapBuffers();
      unmap();
    }
    double dt1 = getSeconds()-t0;

    /* print frame rate */
    std::ostringstream stream;
    stream.setf(std::ios::fixed, std::ios::floatfield);
    stream.precision(2);
    stream << "render: ";
    stream << 1.0f/dt0 << " fps, ";
    stream << dt0*1000.0f << " ms, ";
    stream << "display: ";
    stream << 1.0f/dt1 << " fps, ";
    stream << dt1*1000.0f << " ms, ";
    stream << g_width << "x" << g_height << " pixels";
    std::cout << stream.str() << std::endl;
  }

  void TutorialApplication::reshapeFunc(int width, int height) 
  {
    resize(width,height);
    glViewport(0, 0, width, height);
    g_width = width; g_height = height;
  }
  
  void TutorialApplication::idleFunc()
  {
    glutPostRedisplay();
  }

  void TutorialApplication::initWindowState(int& argc, char** argv, const std::string name, const size_t width, const size_t height, const bool fullscreen, const bool mouseMode)
  {
    g_width = width;
    g_height = height;
    resize(g_width,g_height);

    g_fullscreen = fullscreen;
    flip14 = mouseMode;
    glutInit(&argc, argv);
    glutInitWindowSize((GLsizei)g_width, (GLsizei)g_height);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
    glutInitWindowPosition(0, 0);
    g_window = glutCreateWindow(name.c_str());
    if (g_fullscreen) glutFullScreen();
    glutDisplayFunc(displayFunc);
    glutIdleFunc(idleFunc);
    glutKeyboardFunc(keyboardFunc);
    glutSpecialFunc(specialFunc);
    glutMouseFunc(clickFunc);
    glutMotionFunc(motionFunc);
    glutReshapeFunc(reshapeFunc);
  }
}
