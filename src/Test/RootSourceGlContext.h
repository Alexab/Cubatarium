#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <stdexcept>
struct RootSourceGlContext {
  GLFWwindow *window{};
  RootSourceGlContext(){
    if(!glfwInit())throw std::runtime_error("root fixture glfw initialization");
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    window=glfwCreateWindow(32,32,"root source fixture",nullptr,nullptr);
    if(!window){glfwTerminate();throw std::runtime_error("root fixture actual GL context");}
    glfwMakeContextCurrent(window);glewExperimental=GL_TRUE;
    if(glewInit()!=GLEW_OK){glfwDestroyWindow(window);glfwTerminate();throw std::runtime_error("root fixture glew initialization");}
    while(glGetError()!=GL_NO_ERROR){}
  }
  ~RootSourceGlContext(){if(window)glfwDestroyWindow(window);glfwTerminate();}
};
