//
// Created by morten on 31/07/16.
//

#pragma once
#include "glm/glm.hpp"
#include "SRE/Light.hpp"
#include "SRE/BlendType.hpp"

#include "SRE/Export.hpp"

namespace SRE {
    class Texture;

    /**
     * Controls the apperance of the rendered objects.
     *
     * The shader also controls depth test, depth write and blending.
     *
     * There is the following premade shaders:
     * - Shader::getStandard()
     *    - Shades the mesh using the Phong light model, and uses the current light states and camera states as well as
     *      the color and texture parameters
     *    - Parameters:
     *      - color (vec4) (default white)
     *      - tex (Texture*) (default white texture)
     * - Shader::getUnlit()
     *    - Uses the camera states as well as the color and texture parameters to define the surface color
     *    - Parameters:
     *      - color (vec4) (default white)
     *      - tex (Texture*) (default white texture)
     * - Shader::getUnlitSprite()
     *    - Similar to getUnlit() but with no depth write
     *    - Parameters:
     *      - color (vec4) (default white)
     *      - tex (Texture*) (default white texture)
     */
    class DllExport Shader {
    public:
        // Phong Light Model. Uses light objects and ambient light set in SimpleRenderEngine.
        // Attributes
        // "color" vec4 (default (1,1,1,1))
        // "tex" Texture* (default white texture)
        // "specularity" float (default 0 = no specularity)
        static Shader *getStandard();
        // Unlit model.
        // Attributes
        // "color" vec4 (default (1,1,1,1))
        // "tex" Texture* (default white texture)
        static Shader *getUnlit();
        // UnlitSprite = no depth test and alpha blending
        // Attributes
        // "color" vec4 (default (1,1,1,1))
        // "tex" Texture* (default white texture)
        static Shader *getUnlitSprite();
        // Creates a shader for font rendering
        // "color" vec4 (default (1,1,1,1))
        // "tex" Texture* (default font texture)
        static Shader *getFont();

        static Shader *getDebugUV();
        static Shader *getDebugNormals();

        // Creates shader using GLSL
        static Shader *createShader(const char *vertexShader, const char *fragmentShader);

        ~Shader();

        bool setMatrix(const char *name, glm::mat4 value);
        bool setMatrix(const char *name, glm::mat3 value);

        bool setVector(const char *name, glm::vec4 value);

        bool setFloat(const char *name, float value);

        bool setInt(const char *name, int value);

        /// textureSlot: If sampling multiple textures from a single shader, each texture must be bound to a unique texture slot
        bool setTexture(const char *name, Texture* texture, unsigned int textureSlot = 0);

        void setDepthTest(bool enable);

        bool isDepthTest();

        void setDepthWrite(bool enable);

        bool isDepthWrite();

        BlendType getBlend();

        void setBlend(BlendType blendType);

    private:
        bool setLights(Light value[4], glm::vec4 ambient, glm::mat4 viewTransform);

        static Shader *standard;
        static Shader *unlit;
        static Shader *unlitSprite;
        static Shader *font;

        static Shader *debugUV;
        static Shader *debugNormals;

        Shader();

        void bind();

        unsigned int shaderProgramId;
        bool depthTest = true;
        bool depthWrite = true;
        BlendType blend = BlendType::Disabled;

        friend class Mesh;
        friend class SimpleRenderEngine;
    };
}