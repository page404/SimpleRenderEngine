/*
 *  SimpleRenderEngine (https://github.com/mortennobel/SimpleRenderEngine)
 *
 *  Created by Morten Nobel-Jørgensen ( http://www.nobel-joergnesen.com/ )
 *  License: MIT
 */
#include "sre/Inspector.hpp"
#include "TextEditor.h"
#include <algorithm>
#include <iostream>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/transform.hpp>

#include "sre/Renderer.hpp"
#include "sre/SDLRenderer.hpp"
#include "sre/impl/GL.hpp"
#include "sre/Texture.hpp"
#include "sre/SpriteAtlas.hpp"
#include "sre/Framebuffer.hpp"
#include "sre/Sprite.hpp"
#include "imgui_internal.h"
#include <SDL_image.h>

using Clock = std::chrono::high_resolution_clock;
using Milliseconds = std::chrono::duration<float, std::chrono::milliseconds::period>;

namespace sre {
    namespace {
        std::string appendSize(std::string s, int size) {
            if (size>1){
                s += "["+std::to_string(size)+"]";
            }
            return s;
        }

        std::string glEnumToString(int type) {
            std::string typeStr = "unknown";
            switch (type){
                case GL_FLOAT:
                    typeStr = "float";
                    break;
                case GL_FLOAT_VEC2:
                    typeStr = "vec2";
                    break;
                case GL_FLOAT_VEC3:
                    typeStr = "vec3";
                    break;
                case GL_FLOAT_VEC4:
                    typeStr = "vec4";
                    break;
                case GL_FLOAT_MAT4:
                    typeStr = "mat4";
                    break;
                case GL_FLOAT_MAT3:
                    typeStr = "mat3";
                    break;
#ifndef EMSCRIPTEN
                case GL_INT_VEC4:
                    typeStr = "ivec4";
                    break;
#endif
            }
            return typeStr;
        }
    }

    Inspector::Inspector(int frames,SDLRenderer* sdlRenderer)
            :frames(frames),frameCount(0),sdlRenderer(sdlRenderer)
    {
        stats.resize(frames);
        milliseconds.resize(frames);
        data.resize(frames);
        lastTick = Clock::now();
    }

    void Inspector::showTexture(Texture* tex){
        std::string s = tex->getName()+"##"+std::to_string((int64_t)tex);
        if (ImGui::TreeNode(s.c_str())){

            ImGui::LabelText("Size","%ix%i",tex->getWidth(),tex->getHeight());
            ImGui::LabelText("Cubemap","%s",tex->isCubemap()?"true":"false");
            ImGui::LabelText("Filtersampling","%s",tex->isFilterSampling()?"true":"false");
            ImGui::LabelText("Mipmapping","%s",tex->isMipmapped()?"true":"false");
            ImGui::LabelText("Wrap tex-coords","%s",tex->isWrapTextureCoordinates()?"true":"false");
            ImGui::LabelText("Data size","%f MB",tex->getDataSize()/(1000*1000.0f));
            if (!tex->isCubemap()){
                ImGui::Image(reinterpret_cast<ImTextureID>(tex->textureId), ImVec2(previewSize, previewSize),{0,1},{1,0},{1,1,1,1},{0,0,0,1});
            }

            ImGui::TreePop();
        }
    }

    void Inspector::showMesh(Mesh* mesh){
        std::string s = mesh->getName()+"##"+std::to_string((int64_t)mesh);
        if (ImGui::TreeNode(s.c_str())){
            ImGui::LabelText("Vertex count", "%i", mesh->getVertexCount());
            ImGui::LabelText("Mesh size", "%.2f MB", mesh->getDataSize()/(1000*1000.0f));
            if (ImGui::TreeNode("Vertex attributes")){
                auto attributeNames = mesh->getAttributeNames();
                for (auto a : attributeNames) {
                    auto type = mesh->getType(a);
                    std::string typeStr = glEnumToString(type.first);
                    typeStr = appendSize(typeStr, type.second);
                    ImGui::LabelText(a.c_str(), typeStr.c_str());
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Index sets")) {
                if (mesh->getIndexSets()==0){
                    ImGui::LabelText("", "None");
                } else {
                    for (int i=0;i<mesh->getIndexSets();i++){
                        char res[128];
                        sprintf(res,"Index %i size",i);
                        ImGui::LabelText(res, "%i", mesh->getIndicesSize(i));
                    }
                }
                ImGui::TreePop();
            }
            initFramebuffer();

            Camera camera;
            camera.setPerspectiveProjection(60,0.1,10);
            camera.lookAt({0,0,4},{0,0,0},{0,1,0});
            auto offscreenTexture = getTmpTexture();
            framebuffer->setTexture(offscreenTexture);

            auto renderToTexturePass = RenderPass::create()
                 .withCamera(camera)
                 .withWorldLights(&worldLights)
                 .withFramebuffer(framebuffer)
                 .withClearColor(true, {0, 0, 0, 1})
                 .withGUI(false)
                 .build();
            static auto litMat = Shader::getStandard()->createMaterial();
            static auto unlitMat = Shader::getUnlit()->createMaterial();

            bool hasNormals = mesh->getNormals().size()>0;
            auto mat = hasNormals ? litMat : unlitMat;
            auto sharedPtrMesh = mesh->shared_from_this();
            float rotationSpeed = 0.001f;

            auto bounds = mesh->getBoundsMinMax();
            auto center = (bounds[1] + bounds[0])*0.5f;
            auto offset = -center;
            auto scale = bounds[1]-bounds[0];
            float maxS = std::max({scale.x,scale.y,scale.z});

            std::vector<std::shared_ptr<Material>> mats;
            for (int m = 0;m<std::max(1,sharedPtrMesh->getIndexSets());m++){
                mats.push_back(mat);
            }
            renderToTexturePass.draw(sharedPtrMesh, glm::eulerAngleY(time*rotationSpeed)*glm::scale(glm::vec3{2.0f/maxS,2.0f/maxS,2.0f/maxS})*glm::translate(offset), mats);

            ImGui::Image(reinterpret_cast<ImTextureID>(offscreenTexture->textureId), ImVec2(previewSize, previewSize),{0,1},{1,0},{1,1,1,1},{0,0,0,1});
            ImGui::TreePop();
        }
    }

    std::string glUniformToString(UniformType type);

    void Inspector::showShader(Shader* shader){
        std::string s = shader->getName()+"##"+std::to_string((int64_t)shader);
        if (ImGui::TreeNode(s.c_str())){
            if (ImGui::Button("Edit")) {
                shaderEdit = std::weak_ptr<Shader>(shader->shared_from_this());
            }
            if (ImGui::TreeNode("Attributes")) {
                auto attributeNames = shader->getAttributeNames();
                for (auto a : attributeNames){
                    auto type = shader->getAttibuteType(a);
                    std::string typeStr = glEnumToString(type.first);
                    typeStr = appendSize(typeStr, type.second);
                    ImGui::LabelText(a.c_str(), typeStr.c_str());
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Uniforms")) {
                auto uniformNames = shader->getUniformNames();
                for (auto a : uniformNames){
                    auto type = shader->getUniformType(a);
                    std::string typeStr = glUniformToString(type.type);
                    typeStr = appendSize(typeStr, type.arraySize);
                    ImGui::LabelText(a.c_str(), typeStr.c_str());
                }
                ImGui::TreePop();
            }
            auto blend = shader->getBlend();
            std::string s;
            switch (blend){
                case BlendType::AdditiveBlending:
                    s = "Additive blending";
                    break;
                case BlendType::AlphaBlending:
                    s = "Alpha blending";
                    break;
                case BlendType::Disabled:
                    s = "Disabled";
                    break;

            }
            ImGui::LabelText("Blending","%s",s.c_str());
            ImGui::LabelText("Depth test","%s",shader->isDepthTest()?"true":"false");
            ImGui::LabelText("Depth write","%s",shader->isDepthWrite()?"true":"false");
            ImGui::LabelText("Offset","factor: %.1f units: %.1f",shader->getOffset().x,shader->getOffset().y);

            initFramebuffer();

            auto mat = shader->createMaterial();

            static auto mesh = Mesh::create().withSphere().withName("Preview Shader Mesh").build();

            Camera camera;
            camera.setPerspectiveProjection(60,0.1,10);
            camera.lookAt({0,0,4},{0,0,0},{0,1,0});
            auto offscreenTexture = getTmpTexture();
            framebuffer->setTexture(offscreenTexture);
            auto renderToTexturePass = RenderPass::create()
                    .withCamera(camera)
                    .withWorldLights(&worldLights)
                    .withFramebuffer(framebuffer)
                    .withClearColor(true, {0, 0, 0, 1})
                    .withGUI(false)
                    .build();
            float rotationSpeed = 0.001f;

            renderToTexturePass.draw(mesh, glm::eulerAngleY(time*rotationSpeed), mat);

            ImGui::Image(reinterpret_cast<ImTextureID>(offscreenTexture->textureId), ImVec2(previewSize, previewSize),{0,1},{1,0},{1,1,1,1},{0,0,0,1});
            ImGui::TreePop();
        }
    }

    std::string glUniformToString(UniformType type) {
        switch (type){
            case UniformType::Float:
                return "float";
            case UniformType::Int:
                return "int";
            case UniformType::Mat3:
                return "mat3";
            case UniformType::Mat4:
                return "mat4";
            case UniformType::Texture:
                return "texture";
            case UniformType::TextureCube:
                return "texture cube";
            case UniformType::Vec3:
                return "vec3";
            case UniformType::Vec4:
                return "vec4";
            case UniformType::Invalid:
                return "Unsupported";

        }
        return "Unknown";
}

    void showFramebufferObject(Framebuffer* fbo){
        std::string s = fbo->getName()+"##"+std::to_string((int64_t)fbo);
        if (ImGui::TreeNode(s.c_str())){
            ImGui::TreePop();
        }
    }

    void Inspector::gui(bool useWindow) {
        Renderer* r = Renderer::instance;
        if (useWindow){
            static bool open = true;
            ImGui::Begin("SRE Renderer",&open);
        }

        if (ImGui::CollapsingHeader("Renderer")){

            ImGui::LabelText("SRE Version", "%d.%d.%d",r->sre_version_major, r->sre_version_minor, r->sre_version_point);
            if (sdlRenderer != nullptr){
                ImGui::LabelText("Fullscreen", "%s",sdlRenderer->isFullscreen()?"true":"false");
                ImGui::LabelText("Mouse cursor locked", "%s",sdlRenderer->isMouseCursorLocked()?"true":"false");
                ImGui::LabelText("Mouse cursor visible", "%s",sdlRenderer->isMouseCursorVisible()?"true":"false");
            }
            ImGui::LabelText("Window size", "%ix%i",r->getWindowSize().x,r->getWindowSize().y);
            ImGui::LabelText("Drawable size", "%ix%i",r->getDrawableSize().x,r->getDrawableSize().y);
            ImGui::LabelText("VSync", "%s", r->usesVSync()?"true":"false");

            char* version = (char*)glGetString(GL_VERSION);
            ImGui::LabelText("OpenGL version",version);

            char* vendor = (char*)glGetString(GL_VENDOR);
            ImGui::LabelText("OpenGL vendor", vendor);

            SDL_version compiled;
            SDL_version linked;

            SDL_VERSION(&compiled);
            SDL_GetVersion(&linked);
            ImGui::LabelText("SDL version compiled", "%d.%d.%d",compiled.major, compiled.minor, compiled.patch);
            ImGui::LabelText("SDL version linked", "%d.%d.%d",linked.major, linked.minor, linked.patch);

            linked = *IMG_Linked_Version();
            SDL_IMAGE_VERSION(&compiled);
            ImGui::LabelText("SDL_IMG version compiled","%d.%d.%d",
                   compiled.major,
                   compiled.minor,
                   compiled.patch);
            ImGui::LabelText("SDL version linked", "%d.%d.%d",
                             linked.major, linked.minor, linked.patch);
            ImGui::LabelText("IMGUI version", IMGUI_VERSION);
        }

        if (ImGui::CollapsingHeader("Performance")){
            float max = 0;
            float sum = 0;
            for (int i=0;i<frames;i++){
                int idx = (frameCount + i)%frames;
                float t = milliseconds[idx];
                data[(-frameCount%frames+idx+frames)%frames] = t;
                max = std::max(max, t);
                sum += t;
            }
            float avg = 0;
            if (frameCount > 0){
                avg = sum / std::min(frameCount, frames);
            }
            char res[128];
            sprintf(res,"Avg time: %4.2f ms\nMax time: %4.2f ms",avg,max);

            ImGui::PlotLines(res,data.data(),frames, 0, "Milliseconds", -1,max*1.2f,ImVec2(ImGui::CalcItemWidth(),150));

            max = 0;
            sum = 0;
            for (int i=0;i<frames;i++){
                int idx = (frameCount + i)%frames;
                float t = stats[idx].drawCalls;
                data[(-frameCount%frames+idx+frames)%frames] = t;
                max = std::max(max, t);
                sum += t;
            }
            avg = 0;
            if (frameCount > 0){
                avg = sum / std::min(frameCount, frames);
            }
            sprintf(res,"Avg: %4.1f\nMax: %4.1f",avg,max);

            ImGui::PlotLines(res,data.data(),frames, 0, "Draw calls", -1,max*1.2f,ImVec2(ImGui::CalcItemWidth(),150));

            max = 0;
            sum = 0;
            for (int i=0;i<frames;i++){
                int idx = (frameCount + i)%frames;
                float t = stats[idx].stateChangesShader + stats[idx].stateChangesMaterial + stats[idx].stateChangesMesh;
                data[(-frameCount%frames+idx+frames)%frames] = t;
                max = std::max(max, t);
                sum += t;
            }
            avg = 0;
            if (frameCount > 0){
                avg = sum / std::min(frameCount, frames);
            }
            sprintf(res,"Avg: %4.1f\nMax: %4.1f",avg,max);

            ImGui::PlotLines(res,data.data(),frames, 0, "State changes", -1,max*1.2f,ImVec2(ImGui::CalcItemWidth(),150));
        }
        if (ImGui::CollapsingHeader("Memory")){
            float max = 0;
            float sum = 0;
            for (int i=0;i<frames;i++){
                int idx = (frameCount + i)%frames;
                float t = stats[idx].meshBytes/1000000.0f;
                data[(-frameCount%frames+idx+frames)%frames] = t;
                max = std::max(max, t);
                sum += t;
            }
            float avg = 0;
            if (frameCount > 0){
                avg = sum / std::min(frameCount, frames);
            }
            char res[128];
            sprintf(res,"Avg: %4.1f MB\nMax: %4.1f MB\nCount: %i",avg,max, r->meshes.size());

            ImGui::PlotLines(res,data.data(),frames, 0, "Mesh MB", -1,max*1.2f,ImVec2(ImGui::CalcItemWidth(),150));

            max = 0;
            sum = 0;
            for (int i=0;i<frames;i++){
                int idx = (frameCount + i)%frames;
                float t = stats[idx].textureBytes/1000000.0f;
                data[(-frameCount%frames+idx+frames)%frames] = t;
                max = std::max(max, t);
                sum += t;
            }
            avg = 0;
            if (frameCount > 0){
                avg = sum / std::min(frameCount, frames);
            }
            sprintf(res,"Avg: %4.1f MB\nMax: %4.1f MB\nCount: %i",avg,max,r->textures.size());

            ImGui::PlotLines(res,data.data(),frames, 0, "Texture MB", -1,max*1.2f,ImVec2(ImGui::CalcItemWidth(),150));
        }
        if (ImGui::CollapsingHeader("Shaders")){
            for (auto s : r->shaders){
                showShader(s);
            }
            if (r->shaders.empty()){
                ImGui::LabelText("","No shaders");
            }
        }
        if (ImGui::CollapsingHeader("Textures")){
            for (auto t : r->textures){
                showTexture(t);
            }
            if (r->textures.empty()){
                ImGui::LabelText("","No textures");
            }
        }
        if (ImGui::CollapsingHeader("Meshes")){
            for (auto m : r->meshes){
                showMesh(m);
            }
            if (r->meshes.empty()){
                ImGui::LabelText("","No meshes");
            }
        }
        if (!r->spriteAtlases.empty()){
            if (ImGui::CollapsingHeader("Sprite atlases")){
                for (auto atlas : r->spriteAtlases){
                    showSpriteAtlas(atlas);
                }
            }
        }
        if (!r->framebufferObjects.empty()) {
            if (ImGui::CollapsingHeader("Framebuffer objects")) {
                for (auto fbo : r->framebufferObjects) {
                    showFramebufferObject(fbo);
                }
            }
        }
        if (useWindow) {
            ImGui::End();
        }

        if (auto shaderEditPtr = shaderEdit.lock()){
            editShader(shaderEditPtr.get());
        }
    }

    void Inspector::editShader(Shader* shader){
        static Shader* shaderRef = nullptr;
        static std::vector<std::string> shaderCode;
        static TextEditor textEditor;
        static int selectedShader = 0;
        static bool showPrecompiled = false;

        if (shaderRef != shader){
            shaderRef = shader;
            shaderCode.clear();
            for (auto source : shader->shaderSources){
                auto source_ = Shader::getSource(source.second);
                shaderCode.emplace_back(source_);
            }
            selectedShader = 0;
            textEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::GLSL());
            textEditor.SetText(shaderCode[selectedShader]);
            textEditor.SetPalette(TextEditor::GetDarkPalette());
            showPrecompiled = false;
        }
        bool open = true;
        ImGui::PushID(shader);
        ImGui::Begin(shader->name.c_str(),&open);
        std::vector<const char*> activeShaders;
        std::vector<ShaderType> sources;
        for (auto source : shader->shaderSources){
            sources.push_back(source.first);
            switch (source.first){
                case ShaderType::Vertex:
                    activeShaders.push_back("Vertex");
                    break;
                case ShaderType::Fragment:
                    activeShaders.push_back("Fragment");
                    break;
                case ShaderType::Geometry:
                    activeShaders.push_back("Geometry");
                    break;
                case ShaderType::TessellationControl:
                    activeShaders.push_back("TessellationControl");
                    break;
                case ShaderType::TessellationEvaluation:
                    activeShaders.push_back("TessellationEvaluation");
                    break;
                case ShaderType::NumberOfShaderTypes:
                    LOG_ERROR("ShaderType::NumberOfShaderTypes should never be used");
                    break;
                default:
                    LOG_ERROR("Unhandled shader");
                    break;
            }
        }
        ImGui::PushItemWidth(-1); // align to right
        int lastSelectedShader = selectedShader;
        bool updatedShader = ImGui::Combo("####ShaderType", &selectedShader, activeShaders.data(), static_cast<int>(activeShaders.size()));

        selectedShader = std::min(selectedShader, (int)activeShaders.size());

        bool updatedPrecompile = ImGui::Checkbox("Show precompiled", &showPrecompiled); ImGui::SameLine();
        if (updatedPrecompile){
            textEditor.SetPalette(showPrecompiled? TextEditor::GetLightPalette():TextEditor::GetDarkPalette());
        }
        bool compile = ImGui::Button("Compile");

        // update if compile or shader type changed or showPrecompiled is selected
        if ((compile && !showPrecompiled) || (updatedShader && !showPrecompiled) || (updatedPrecompile && showPrecompiled)){
            shaderCode[lastSelectedShader] = textEditor.GetText(); // get text before updating the editor
        }

        if (compile){
            auto builder = shader->update();
            for (int i=0;i<sources.size();i++){
                builder.withSourceString(shaderCode[i], sources[i]);
            }
            builder.build();
        }

        if (updatedShader || updatedPrecompile){
            if (showPrecompiled){
                textEditor.SetText(Shader::precompile(shaderCode[selectedShader]));
                textEditor.SetReadOnly(true);
            } else {
                textEditor.SetText(shaderCode[selectedShader]);
                textEditor.SetReadOnly(false);
            }
        }
        textEditor.Render("##editor");

        ImGui::End();
        ImGui::PopID();

        if (!open) {
            shaderEdit.reset();
        }
    }

    void Inspector::update() {
        usedTextures = 0;
        auto tick = Clock::now();
        float deltaTime = std::chrono::duration_cast<Milliseconds>(tick - lastTick).count();
        time += deltaTime;
        lastTick = tick;

        stats[frameCount%frames] = Renderer::instance->getRenderStats();
        milliseconds[frameCount%frames] = deltaTime;
        frameCount++;
    }

    void Inspector::showSpriteAtlas(SpriteAtlas *pAtlas) {
        std::string s = pAtlas->getAtlasName()+"##"+std::to_string((int64_t)pAtlas);
        if (ImGui::TreeNode(s.c_str())){
            std::stringstream ss;
            for (auto& str : pAtlas->getNames()){
                ss<< str<<'\0';
            }
            ss<< '\0';
            auto ss_str = ss.str();
            static std::map<SpriteAtlas *,int> spriteAtlasSelection;
            auto elem = spriteAtlasSelection.find(pAtlas);
            if (elem == spriteAtlasSelection.end()){
                spriteAtlasSelection.insert({pAtlas, -1});
                elem = spriteAtlasSelection.find(pAtlas);
            }
            int* index = &elem->second;
            ImGui::Combo("Sprite names", index, ss_str.c_str());

            if (*index != -1){
                auto name = pAtlas->getNames()[*index];
                Sprite sprite = pAtlas->get(name);
                ImGui::LabelText("Sprite anchor","(%.2f,%.2f)",sprite.getSpriteAnchor().x,sprite.getSpriteAnchor().y);
                ImGui::LabelText("Sprite size","%ix%i",sprite.getSpriteSize().x,sprite.getSpriteSize().y);
                ImGui::LabelText("Sprite pos","(%i,%i)",sprite.getSpritePos().x,sprite.getSpritePos().y);
                auto tex = sprite.texture;
                auto uv0 = ImVec2((sprite.getSpritePos().x)/(float)tex->getWidth(), (sprite.getSpritePos().y+sprite.getSpriteSize().y)/(float)tex->getHeight());
                auto uv1 = ImVec2((sprite.getSpritePos().x+sprite.getSpriteSize().x)/(float)tex->getWidth(),(sprite.getSpritePos().y)/(float)tex->getHeight());
                ImGui::Image(reinterpret_cast<ImTextureID>(tex->textureId), ImVec2(previewSize/sprite.getSpriteSize().y*(float)sprite.getSpriteSize().x, previewSize),uv0, uv1,{1,1,1,1},{0,0,0,1});
            }

            ImGui::TreePop();
        }
    }

    void Inspector::initFramebuffer() {
        if (framebuffer == nullptr){
            framebuffer = Framebuffer::create().withTexture(getTmpTexture()).withName("SRE Inspector Framebufferobject").build();
            usedTextures = 0; // reset usedTextures count to avoid an extra texture to be created
            worldLights.setAmbientLight({0.2,0.2,0.2});
            auto light = Light::create().withPointLight({0,0,4}).build();
            worldLights.addLight(light);
        }
    }

    std::shared_ptr<Texture> Inspector::getTmpTexture() {
        if (usedTextures < offscreenTextures.size()){
            int index = usedTextures;
            usedTextures++;
            return offscreenTextures[index];
        }
        auto offscreenTex = Texture::create().withRGBData(nullptr, 256,256).withName(std::string("SRE Inspector Tex #")+std::to_string(offscreenTextures.size())).build();
        offscreenTextures.push_back(offscreenTex);
        usedTextures++;
        return offscreenTex;
    }
}