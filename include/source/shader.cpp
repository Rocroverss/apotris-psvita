#ifdef GL
#include "shader.h"
#include "liba_pc.h"

#ifdef PC
#include <glad/gl.h>
#elif defined(PORTMASTER)
#include <glad/gles2.h>
#endif

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "logging.h"
#include <cstdio>
#include <dirent.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef ANDROID
#include "liba_android.h"

#include <android/log.h>
#include <glad/gles2.h>
#define LOG_TAG "ApotrisShader"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#else
#define LOGE(...) printf(__VA_ARGS__)
#define LOGI(...) printf(__VA_ARGS__)
#endif

int width = 0;
int height = 0;
float scale = 0;

uint vbo, ebo, vao, shaderTexture, shaderProgram;

SDL_GLContext shaderContext;

float vertices[] = {
    1.0f,  1.0f,  0.0f, 1.0f, 0.0f, // top right
    1.0f,  -1.0f, 0.0f, 1.0f, 1.0f, // bottom right
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, // bottom left
    -1.0f, 1.0f,  0.0f, 0.0f, 0.0f, // top left
};

uint indices[] = {
    // note that we start from 0!
    0, 1, 3, // first triangle
    1, 2, 3  // second triangle
};

std::string translateLine(std::string in, bool isVertex) {

    std::size_t pos = in.find("out vec4");

    if (pos != std::string::npos)
        in.replace(pos, 8, "out highp vec4");

#ifdef __APPLE__
    // Precision qualifiers for macOS
    pos = in.find("uniform vec2");
    if (pos != std::string::npos)
        in.replace(pos, 12, "uniform mediump vec2");
#endif

    pos = in.find("varying");
    if (pos != std::string::npos) {
        if (isVertex) {
            in.replace(pos, 7, "out");
        } else {
            in.replace(pos, 7, "in");
        }
    }

    pos = in.find("attribute");
    if (pos != std::string::npos) {
        if (isVertex) {
            in.replace(pos, 9, "in");
        }
    }

    pos = in.find("texture2D");
    if (pos != std::string::npos) {
        in.replace(pos, 9, "texture");
    }

    pos = in.find("gl_FragColor");
    if (pos != std::string::npos) {
        in.replace(pos, 12, "FragColor");
    }

    return in;
}

uint loadShaderFromFile(std::string path, int type) {
    std::string add = "";

#if defined(PORTMASTER) || defined(ANDROID)
    add += "#version 300 es\n\n";
#elif defined(__APPLE__)
    add += "#version 410 core\n\n";
#endif

    if (type == GL_VERTEX_SHADER) {
        add += "#define VERTEX\n\n";
    } else if (type == GL_FRAGMENT_SHADER) {
        add += "#define FRAGMENT\n\n";
    }

    std::string buffer_out;

    SDL_RWops* file = SDL_RWFromFile(path.c_str(), "rb");
    if (file) {
        Sint64 size = SDL_RWsize(file);
        char* content = (char*)malloc(size + 1);
        SDL_RWread(file, content, size, 1);
        content[size] = 0;
        SDL_RWclose(file);

        // Check if the shader already declares FragColor
        std::string fileContent(content);
        if (type == GL_FRAGMENT_SHADER) {
            if (fileContent.find("out vec4 FragColor") == std::string::npos &&
                fileContent.find("out mediump vec4 FragColor") ==
                    std::string::npos &&
                fileContent.find("out COMPAT_PRECISION vec4 FragColor") ==
                    std::string::npos) {
                add += "out mediump vec4 FragColor;\n";
            }
        }

        std::stringstream buffer_in(content);
        free(content);

        std::string line;
        while (getline(buffer_in, line)) {
            if (line.find("pragma") == std::string::npos) {
                buffer_out +=
                    translateLine(line, type == GL_VERTEX_SHADER) + "\n";
            }
        }
    } else {
        std::cout << "Failed to open shader file: " << path << std::endl;
        LOGE("Failed to open shader file: %s", path.c_str());
        return 0;
    }

    std::string str = add + buffer_out;

    uint shader = glCreateShader(type);
    const char* sourcePtr = str.c_str();
    glShaderSource(shader, 1, &sourcePtr, NULL);
    glCompileShader(shader);

    int success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (!success) {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::COMPILATION_FAILED for " << path << "\n"
                  << "Error: " << infoLog << std::endl;
        LOGE("ERROR::SHADER::COMPILATION_FAILED for %s\nError: %s",
             path.c_str(), infoLog);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

uint getShader(int index) {
    std::vector<std::string> shaders = findShaders();

    if (shaders.size() == 0)
        return 0;

    if (index <= 0 || index > (int)shaders.size()) {
        return 0;
    }

    std::string shaderFile = shaders.at(index - 1);

    log("loading shader: " + shaderFile);

    uint vertexShader = loadShaderFromFile(shaderFile, GL_VERTEX_SHADER);
    if (vertexShader == 0)
        return 0;

    uint fragmentShader = loadShaderFromFile(shaderFile, GL_FRAGMENT_SHADER);
    if (fragmentShader == 0) {
        glDeleteShader(vertexShader);
        return 0;
    }

    uint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    int success;
    char infoLog[512];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::LINKING_FAILED\n" << infoLog << std::endl;
        LOGE("ERROR::SHADER::LINKING_FAILED\n%s", infoLog);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

void initShaders(SDL_Window* window, int index) {
#ifdef __APPLE__
    // Request OpenGL 4.1 Core Profile (macOS maximum)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#endif

#ifdef ANDROID
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#endif

    shaderContext = SDL_GL_CreateContext(window);

    if (shaderStatus == ShaderStatus::NOT_INITED) {
#ifdef PC
        int version = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);
#else
        int version = gladLoadGLES2((GLADloadfunc)SDL_GL_GetProcAddress);
#endif

        printf("GL %d.%d\n", GLAD_VERSION_MAJOR(version),
               GLAD_VERSION_MINOR(version));

        if (version == 0) {
            shaderStatus = ShaderStatus::NO_GL;
            LOGE("Failed to initialize GLAD");
            return;
        }

#ifdef ANDROID
        if (GLAD_VERSION_MAJOR(version) < 3) {
            LOGE("OpenGL ES 3.0 required but found %d.%d",
                 GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));
            shaderStatus = ShaderStatus::NO_GL;
            return;
        }
#endif
    }

    glViewport(0, 0, width, height);

    shaderProgram = getShader(index);

    if (shaderProgram == 0) {
        shaderStatus = ShaderStatus::NO_SHADER_FILES;
        LOGE("Failed to get shader program");
        return;
    }

    int res_out = glGetUniformLocation(shaderProgram, "OutputSize");
    if (res_out == -1) {
        LOGI("WARNING: OutputSize uniform not found (optimized out?)");
    } else {
        glUniform2f(res_out, (float)width, (float)height);
    }

    int res_in = glGetUniformLocation(shaderProgram, "InputSize");
    if (res_in == -1) {
        LOGI("WARNING: InputSize uniform not found (optimized out?)");
    } else {
        glUniform2f(res_in, (float)width, (float)height);
    }

    int res_tex = glGetUniformLocation(shaderProgram, "TextureSize");
    if (res_tex == -1) {
        LOGI("WARNING: TextureSize uniform not found (optimized out?)");
    } else {
        glUniform2f(res_tex, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT);
    }

    glUseProgram(shaderProgram);
    LOGI("Shader program used");

    glGenBuffers(1, &vbo);
    LOGI("Generated VBO: %u", vbo);
    glGenBuffers(1, &ebo);
    LOGI("Generated EBO: %u", ebo);

    if (glad_glGenVertexArrays == NULL) {
        LOGE("CRITICAL: glGenVertexArrays is NULL!");
    } else {
        glGenVertexArrays(1, &vao);
        LOGI("Generated VAO: %u", vao);
    }

    glGenTextures(1, &shaderTexture);
    LOGI("Generated Texture: %u", shaderTexture);

    if (glad_glBindVertexArray == NULL) {
        LOGE("CRITICAL: glBindVertexArray is NULL!");
    } else {
        glBindVertexArray(vao);
        LOGI("Bound VAO");
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    LOGI("Buffer Data VBO set");

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                 GL_STATIC_DRAW);
    LOGI("Buffer Data EBO set");

    int position = glGetAttribLocation(shaderProgram, "VertexCoord");
    if (position == -1) {
        log("ERROR: position");
        LOGE("ERROR: position");
        shaderStatus = ShaderStatus::SHADER_FILE_ERROR;
        return;
    }
    LOGI("VertexCoord location: %d", position);

    glVertexAttribPointer(position, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)0);
    glEnableVertexAttribArray(position);
    LOGI("Vertex Attrib Pointer set");

    int texCoord = glGetAttribLocation(shaderProgram, "TexCoord");
    if (texCoord == -1) {
        log("ERROR: texcoord");
        LOGE("ERROR: texcoord");
        shaderStatus = ShaderStatus::SHADER_FILE_ERROR;
        return;
    }
    LOGI("TexCoord location: %d", texCoord);

    glVertexAttribPointer(texCoord, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(texCoord);
    LOGI("TexCoord Attrib Pointer set");

    glm::mat4 def = glm::mat4(1.0f); // Identity matrix
    LOGI("Created identity matrix");

    unsigned int transformLoc =
        glGetUniformLocation(shaderProgram, "MVPMatrix");
    LOGI("Got MVPMatrix location: %u", transformLoc);

    if (transformLoc == -1) {
        log("ERROR: MVP");
        LOGE("ERROR: MVP");
        shaderStatus = ShaderStatus::SHADER_FILE_ERROR;
        return;
    }

    // if (shaderStatus == ShaderStatus::SHADER_FILE_ERROR)
    //     return;

    glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(def));
    LOGI("Set MVPMatrix uniform");

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shaderTexture);
    LOGI("Bound Texture0");

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    LOGI("Set Texture Parameters");

    shaderStatus = ShaderStatus::OK;
    LOGI("Shader initialization complete");
}

void drawWithShaders(SDL_Window* window, SDL_Surface* img, bool shadersEnabled,
                     bool swap) {

    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shaderTexture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCREEN_WIDTH, SCREEN_HEIGHT, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img->pixels);

    if (shadersEnabled) {
        glUseProgram(shaderProgram);
    }

    if (glad_glBindVertexArray) {
        glBindVertexArray(vao);
    }

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    if (swap)
        SDL_GL_SwapWindow(window);
}

void freeShaders() {
    if (shaderStatus == ShaderStatus::NOT_INITED)
        return;
    SDL_GL_DeleteContext(shaderContext);

    shaderStatus = ShaderStatus::NOT_INITED;
    if ((int)shaderStatus <= (int)ShaderStatus::NO_SHADER_FILES) {
        return;
    }

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteTextures(1, &shaderTexture);
    glDeleteProgram(shaderProgram);
}

void refreshShaderResolution(int w, int h, float s) {
    if (shaderStatus != ShaderStatus::OK) {
        LOGE("refreshShaderResolution called but shaders not initialized!");
        return;
    }
    LOGI("refreshShaderResolution start");
    width = w;
    height = h;
    scale = s;

    glViewport(0, 0, width, height);
    LOGI("Set Viewport");

    int res_out = glGetUniformLocation(shaderProgram, "OutputSize");
    glUniform2f(res_out, (float)width, (float)height);

    int res_in = glGetUniformLocation(shaderProgram, "InputSize");
    glUniform2f(res_in, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT);

    int res_tex = glGetUniformLocation(shaderProgram, "TextureSize");
    glUniform2f(res_tex, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT);
    LOGI("Set Uniforms");

    float offsetX = (1.0 - ((width / scale) / SCREEN_WIDTH)) / 2;
    float offsetY = (1.0 - ((height / scale) / SCREEN_HEIGHT)) / 2;

    vertices[0 * 5 + 3] = 1.0 - offsetX; // top right
    vertices[0 * 5 + 4] = offsetY;

    vertices[1 * 5 + 3] = 1.0 - offsetX; // bottom right
    vertices[1 * 5 + 4] = 1.0 - offsetY;

    vertices[2 * 5 + 3] = offsetX; // bottom left
    vertices[2 * 5 + 4] = 1.0 - offsetY;

    vertices[3 * 5 + 3] = offsetX; // top left
    vertices[3 * 5 + 4] = offsetY;

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    LOGI("refreshShaderResolution end");
}

static bool hasSuffix(const std::string& s, const std::string& suffix) {
    return (s.size() >= suffix.size()) &&
           equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

std::vector<std::string> findShaders() {
    std::string shaderPath = "assets/shaders";

#ifdef ANDROID
    shaderPath = getDocumentsPath() + shaderPath;
#endif

    std::vector<std::string> shaderPaths;
    DIR* dir = opendir(shaderPath.c_str());
    if (!dir) {
        log("couldn't open dir: " + shaderPath);
        return {};
    }

    dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (hasSuffix(entry->d_name, ".glsl")) {
            shaderPaths.push_back(shaderPath + "/" + entry->d_name);
        }
    }

    closedir(dir);
    sort(shaderPaths.begin(), shaderPaths.end());
    return shaderPaths;
}

void makeShaderContextCurrent(SDL_Window* window) {
    if (shaderStatus == ShaderStatus::OK && shaderContext != NULL) {
        SDL_GL_MakeCurrent(window, shaderContext);
    }
}
#endif
