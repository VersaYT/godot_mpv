#include "mpv_player.h"
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <iostream>

using namespace godot;

// Static member for callback context
static MPVPlayer* g_instance = nullptr;

void* load_func(const char* name) {
    return (void*)eglGetProcAddress(name);
}

void MPVPlayer::_bind_methods() {
    // Register methods
    ClassDB::bind_method(D_METHOD("initialize"), &MPVPlayer::initialize);
    ClassDB::bind_method(D_METHOD("load_file", "path"), &MPVPlayer::load_file);
    ClassDB::bind_method(D_METHOD("play"), &MPVPlayer::play);
    ClassDB::bind_method(D_METHOD("pause"), &MPVPlayer::pause);
    ClassDB::bind_method(D_METHOD("stop"), &MPVPlayer::stop);
    
    // Video mesh methods
    ClassDB::bind_method(D_METHOD("create_video_mesh_2d"), &MPVPlayer::create_video_mesh_2d);
    ClassDB::bind_method(D_METHOD("create_video_mesh_3d"), &MPVPlayer::create_video_mesh_3d);
    ClassDB::bind_method(D_METHOD("apply_to_mesh_3d", "mesh_instance"), &MPVPlayer::apply_to_mesh_3d);
    ClassDB::bind_method(D_METHOD("apply_to_viewport", "viewport"), &MPVPlayer::apply_to_viewport);
    
    // Getters
    ClassDB::bind_method(D_METHOD("get_debug_level"), &MPVPlayer::get_debug_level);
    ClassDB::bind_method(D_METHOD("get_texture"), &MPVPlayer::get_texture);
    ClassDB::bind_method(D_METHOD("get_width"), &MPVPlayer::get_width);
    ClassDB::bind_method(D_METHOD("get_height"), &MPVPlayer::get_height);

    // Setters
    ClassDB::bind_method(D_METHOD("set_debug_level"), &MPVPlayer::set_debug_level);
    
    // Register signals
    ADD_SIGNAL(MethodInfo("texture_updated", PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D")));

}

// Static callback for MPV render updates
void MPVPlayer::on_mpv_render_update(void* ctx) {
    MPVPlayer* instance = static_cast<MPVPlayer*>(ctx);
    if (instance) {
        // Don't call any Godot functions from this callback
        // Just set the flag and let the main thread handle it
        instance->frame_available.store(true);
    }
}

unsigned int MPVPlayer::get_debug_level() {
    return debug_level;
}

void MPVPlayer::set_debug_level(unsigned int level) {
    debug_level = level;
}

MPVPlayer::MPVPlayer() : 
    debug_level(DEBUG_NONE),
    mpv(nullptr), 
    mpv_ctx(nullptr),
    fbo(0),
    texture(0),
    width(1280),
    height(720),
    video_mesh(nullptr),
    video_mesh_3d(nullptr),
    running(false),
    frame_available(false),
    texture_needs_update(false),
    has_new_frame(false),
    is_streaming(false),
    frame_count(0),
    stream_frame_threshold(30), // Allow up to 30 black frames for streaming
    egl_display(EGL_NO_DISPLAY),
    egl_surface(EGL_NO_SURFACE),
    egl_context(EGL_NO_CONTEXT) {
    
    g_instance = this;
    
    // Initialize image with default data to avoid "empty image" errors
    frame_image.instantiate();
    frame_image->create(width, height, false, Image::FORMAT_RGBA8);
    
    // Fill with black pixels
    PackedByteArray initial_data;
    initial_data.resize(width * height * 4);
    for (int i = 0; i < initial_data.size(); i += 4) {
        initial_data.set(i, 0);     // R
        initial_data.set(i + 1, 0); // G
        initial_data.set(i + 2, 0); // B
        initial_data.set(i + 3, 255); // A (fully opaque)
    }
    frame_image->set_data(width, height, false, Image::FORMAT_RGBA8, initial_data);
    
    // Create texture from the initialized image
    frame_texture = ImageTexture::create_from_image(frame_image);
    
    // Initialize pixel data buffer
    pixel_data.resize(width * height * 4);
    pending_frame_data.resize(width * height * 4);
}

MPVPlayer::~MPVPlayer() {
    // Stop the render thread
    if (running.load()) {
        running.store(false);
        if (render_thread.joinable()) {
            render_thread.join();
        }
    }
    
    // Clean up MPV resources
    if (mpv_ctx) {
        mpv_render_context_free(mpv_ctx);
        mpv_ctx = nullptr;
    }
    
    if (mpv) {
        mpv_terminate_destroy(mpv);
        mpv = nullptr;
    }
    
    // Clean up OpenGL resources
    if (fbo != 0) {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
    
    if (texture != 0) {
        glDeleteTextures(1, &texture);
        texture = 0;
    }
    
    // Reset static instance
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

void MPVPlayer::_notification(int p_what) {
    switch (p_what) {
        case NOTIFICATION_PREDELETE: {
            // Stop the render thread
            if (running.load()) {
                running.store(false);
                if (render_thread.joinable()) {
                    render_thread.join();
                }
            }
            
            // Clean up MPV resources
            if (mpv_ctx) {
                mpv_render_context_free(mpv_ctx);
                mpv_ctx = nullptr;
            }
            
            if (mpv) {
                mpv_terminate_destroy(mpv);
                mpv = nullptr;
            }
            
            // Clean up OpenGL resources
            if (egl_display != EGL_NO_DISPLAY) {
                if (egl_context != EGL_NO_CONTEXT) {
                    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                    eglDestroyContext(egl_display, egl_context);
                    egl_context = EGL_NO_CONTEXT;
                }
                
                if (egl_surface != EGL_NO_SURFACE) {
                    eglDestroySurface(egl_display, egl_surface);
                    egl_surface = EGL_NO_SURFACE;
                }
                
                eglTerminate(egl_display);
                egl_display = EGL_NO_DISPLAY;
            }
            
            // Clean up OpenGL resources
            if (fbo != 0) {
                glDeleteFramebuffers(1, &fbo);
                fbo = 0;
            }
            
            if (texture != 0) {
                glDeleteTextures(1, &texture);
                texture = 0;
            }
            
            break;
        }
    }
}

bool MPVPlayer::initialize() {
    if(debug_level == DEBUG_SIMPLE || debug_level == DEBUG_FULL)
    UtilityFunctions::print("Starting MPV player initialization");
    
    // Create MPV instance
    mpv = mpv_create();
    if (!mpv) {
        UtilityFunctions::print("Failed to create MPV instance");
        return false;
    }
    
    // Set basic MPV options
    mpv_set_option_string(mpv, "vo", "libmpv");
    mpv_set_option_string(mpv, "hwdec", "sw");
    // mpv_set_option_string(mpv, "video-sync", "display");
    
    // Set network-related options for better HTTP streaming support
    mpv_set_option_string(mpv, "network-timeout", "15"); // 15 seconds timeout
    mpv_set_option_string(mpv, "user-agent", "Mozilla/5.0 Godot/MPV Player"); // Set a user agent
    
    // Enable verbose logging in debug mode
    if (debug_level == DEBUG_FULL) {
        mpv_request_log_messages(mpv, "v"); // Verbose logging
        mpv_set_option_string(mpv, "msg-level", "all=v");
    }
    
    // Initialize MPV
    int init_result = mpv_initialize(mpv);
    if (init_result < 0) {
        UtilityFunctions::print("Failed to initialize MPV: ", mpv_error_string(init_result));
        return false;
    }
    
    // Set up property observation for debugging
    if (debug_level == DEBUG_FULL) {
        // Observe network-related properties
        mpv_observe_property(mpv, 0, "demuxer-cache-state", MPV_FORMAT_NODE);
        mpv_observe_property(mpv, 0, "paused-for-cache", MPV_FORMAT_FLAG);
    }
    
    // Initialize OpenGL rendering
    if (!initialize_gl()) {
        UtilityFunctions::print("Failed to initialize OpenGL");
        return false;
    }
    
    // Set up MPV render context
    mpv_opengl_init_params gl_init_params = {
        .get_proc_address = MPVPlayer::get_proc_address_mpv,
        .get_proc_address_ctx = nullptr
    };
    
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    
    if (mpv_render_context_create(&mpv_ctx, mpv, params) < 0) {
        UtilityFunctions::print("Failed to create MPV render context");
        return false;
    }
    
    // Set up render update callback
    if(debug_level == DEBUG_SIMPLE || debug_level == DEBUG_FULL)
    UtilityFunctions::print("Setting up render update callback");
    mpv_render_context_set_update_callback(mpv_ctx, on_mpv_render_update, this);
    
    // We'll start the render thread in _ready to ensure all Godot objects are properly initialized
    // This helps avoid thread safety issues
    if(debug_level == DEBUG_SIMPLE || debug_level == DEBUG_FULL)
    UtilityFunctions::print("MPV player initialized");
    return true;
}

void MPVPlayer::_ready() {
    // Start render thread
    running.store(true);
    render_thread = std::thread(&MPVPlayer::render_loop, this);
}

bool MPVPlayer::initialize_gl() {
    if(debug_level == DEBUG_SIMPLE || debug_level == DEBUG_FULL)
    UtilityFunctions::print("Initializing OpenGL for MPV rendering");
    
    // init and load openGL on windows
    #ifdef _WIN32
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        UtilityFunctions::print("failed to init egl display");
    }

    if (!eglInitialize(display, nullptr, nullptr)) {
        UtilityFunctions::print("failed to init egl");
    }

    const EGLint config_attribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_NONE
    };

    EGLConfig config;
    EGLint num_configs;
    if (!eglChooseConfig(display, config_attribs, &config, 1, &num_configs)) {
        UtilityFunctions::print("failed to apply egl config");
    }
    
    const EGLint pbuffer_attribs[] = {
    EGL_WIDTH, 1,
    EGL_HEIGHT, 1,
    EGL_NONE
    };
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbuffer_attribs);

    const EGLint context_attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
    };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);

    if (!eglMakeCurrent(display, surface, surface, context)) {
        UtilityFunctions::print("could not make egl context current");
    }
    
    if (!gladLoadGLES2((GLADloadfunc)load_func)) {
        UtilityFunctions::print("eglGetProcName failed");
    }

    #endif

    // Create FBO for rendering
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    
    // Create texture for the FBO
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Allocate texture storage
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    
    // Attach texture to FBO
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    
    // Check FBO status
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        UtilityFunctions::print("ERROR: Framebuffer is not complete: ", status);
        return false;
    }
    
    // Initialize pixel data buffer
    pixel_data.resize(width * height * 4);
    pending_frame_data.resize(width * height * 4);
    
    // Unbind FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if(debug_level == DEBUG_SIMPLE || debug_level == DEBUG_FULL)
    UtilityFunctions::print("OpenGL initialized successfully");
    return true;
}

void MPVPlayer::render_loop() {
    // This is a minimal render loop that only handles MPV render updates
    // It avoids any direct OpenGL operations that might cause thread safety issues
    
    while (running.load()) {
        // Check if MPV needs rendering
        if (frame_available.load()) {
            frame_available.store(false);
            
            // Signal the main thread that we need to update the texture
            // The actual rendering will be done in the main thread
            texture_needs_update.store(true);
        }
        
        // Sleep to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }
}

void MPVPlayer::update_texture() {
    // This method is called from the main thread
    
    // Check if there's new frame data available
    if (texture_needs_update.load()) {
        _update_texture_internal();
    } else {
        // Make sure MPV updates its internal state
        if (mpv && mpv_ctx) {
            mpv_render_context_update(mpv_ctx);
        }
    }
}

void MPVPlayer::_update_texture_internal() {
    // This method runs on the main thread
    
    // Create a new local image and update it with the pixel data
    Ref<Image> new_image;
    new_image.instantiate();
    new_image->create(width, height, false, Image::FORMAT_RGBA8);
    
    {
        std::lock_guard<std::mutex> lock(frame_mutex);
        
        // Don't flip the image - use the data as is since it's already in the correct orientation for 3D
        // Just copy the data directly
        new_image->set_data(width, height, false, Image::FORMAT_RGBA8, pending_frame_data);
    }
    
    // Create a new texture from the image
    Ref<ImageTexture> new_texture = ImageTexture::create_from_image(new_image);
    
    // Only update the reference if we successfully created a new texture
    if (new_texture.is_valid()) {
        frame_image = new_image;
        frame_texture = new_texture;
        
        // Add debug info to verify texture content
        if(debug_level == DEBUG_FULL)
            UtilityFunctions::print("Created texture with size: ", width, "x", height);
        
        // Emit signal for texture update (useful for 3D and SubViewport usage)
        if(debug_level == DEBUG_FULL)
            UtilityFunctions::print("Emitting texture_updated signal");
        emit_signal("texture_updated", frame_texture);
    } else {
        UtilityFunctions::print("ERROR: Failed to create valid texture from image");
    }
}

void MPVPlayer::_process(double delta) {
    // This method runs on the main thread
    
    // Check if we need to update the texture
    if (texture_needs_update.load()) {
        // Reset the flag at the beginning to avoid missing frames
        texture_needs_update.store(false);
        
        // Perform all OpenGL operations on the main thread
        if (mpv_ctx && fbo) {
            // Bind our FBO for rendering
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            
            // Set up FBO for rendering
            mpv_opengl_fbo mpv_fbo = {
                .fbo = static_cast<int>(fbo),
                .w = width,
                .h = height,
                .internal_format = 0
            };
            
            // Rendering parameters
            mpv_render_param params[] = {
                {MPV_RENDER_PARAM_OPENGL_FBO, &mpv_fbo},
                {MPV_RENDER_PARAM_INVALID, nullptr}
            };
            
            // Render frame to FBO
            int render_result = mpv_render_context_render(mpv_ctx, params);
            
            // Read pixels from FBO
            {
                std::lock_guard<std::mutex> lock(frame_mutex);
                
                // Make sure we're in the correct framebuffer
                glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                
                // Don't clear the buffer as it would erase the MPV rendering
                // glClear(GL_COLOR_BUFFER_BIT);
                
                // Read pixels - make sure we're reading RGBA data
                glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixel_data.ptrw());
                
                // Verify we have actual data
                bool has_content = false;
                uint8_t* data = (uint8_t*)pixel_data.ptrw();
                
                // For HTTP streams, always process frames even if they're black
                if (is_streaming) {
                    // For streaming, we'll always accept frames and update the texture
                    has_content = true;
                    frame_count++;
                    
                    if (frame_count % 10 == 0 || frame_count < 5) { // Reduce log spam
                        if(debug_level == DEBUG_FULL)
                        UtilityFunctions::print("Streaming mode: processing frame #", frame_count);
                    }
                    
                    // Check if this frame has actual content (for debugging only)
                    bool has_visible_content = false;
                    for (int i = 0; i < width * height * 4; i += 4) {
                        if (data[i] != 0 || data[i+1] != 0 || data[i+2] != 0) {
                            has_visible_content = true;
                            break;
                        }
                    }
                    
                    if (has_visible_content && !had_visible_content) {
                        had_visible_content = true;
                        UtilityFunctions::print("First non-black frame detected at frame #", frame_count);
                    }
                } else {
                    // For local files, check for actual content
                    for (int i = 0; i < width * height * 4; i += 4) {
                        if (data[i] != 0 || data[i+1] != 0 || data[i+2] != 0) {
                            has_content = true;
                            break;
                        }
                    }
                }
                
                if (has_content) {
                    memcpy(pending_frame_data.ptrw(), pixel_data.ptr(), width * height * 4);
                    has_new_frame.store(true);
                    
                    if(debug_level == DEBUG_FULL)
                        UtilityFunctions::print("Frame data captured");
                } else {
                    if(debug_level == DEBUG_SIMPLE || debug_level == DEBUG_FULL)
                        UtilityFunctions::print("WARNING: Frame data is empty (all black)");
                }
            }
            
            // Unbind our FBO to restore the default framebuffer
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            
            // Now update the texture with the new frame data
            _update_texture_internal();
        }
    }
    
    // Handle any logging that was requested from the render thread
    if (has_new_frame.load()) {
        has_new_frame.store(false);
    }
    
    // Make sure MPV updates its internal state
    if (mpv) {
        mpv_event* event = mpv_wait_event(mpv, 0);
        while (event->event_id != MPV_EVENT_NONE) {
            // Process MPV events
            switch (event->event_id) {
                case MPV_EVENT_FILE_LOADED:
                    if(debug_level == DEBUG_SIMPLE || debug_level == DEBUG_FULL)
                        UtilityFunctions::print("File loaded successfully");
                    
                    // Reset frame counter and content flag when a new file is loaded
                    frame_count = 0;
                    had_visible_content = false;
                    break;
                    
                case MPV_EVENT_PLAYBACK_RESTART:
                    if(debug_level == DEBUG_SIMPLE || debug_level == DEBUG_FULL)
                        UtilityFunctions::print("Playback restarted");
                    break;
                    
                case MPV_EVENT_END_FILE:
                    {
                        mpv_event_end_file* end_file = static_cast<mpv_event_end_file*>(event->data);
                        
                        // Always log errors
                        if (end_file->reason == MPV_END_FILE_REASON_ERROR) {
                            UtilityFunctions::print("ERROR: Playback failed with error code: ", end_file->error);
                            
                            // Get the error string
                            const char* err_str = mpv_error_string(end_file->error);
                            if (err_str) {
                                UtilityFunctions::print("MPV Error: ", err_str);
                            }
                            
                            // For streaming, try to provide more specific error information
                            if (is_streaming) {
                                UtilityFunctions::print("HTTP stream playback failed. Possible causes:");
                                UtilityFunctions::print("- Network connectivity issues");
                                UtilityFunctions::print("- Unsupported codec or format");
                                UtilityFunctions::print("- Invalid URL or stream");
                                
                                // Try to get more diagnostic information
                                char* media_title = nullptr;
                                if (mpv_get_property(mpv, "media-title", MPV_FORMAT_STRING, &media_title) >= 0 && media_title) {
                                    UtilityFunctions::print("Media title: ", media_title);
                                    mpv_free(media_title);
                                }
                            }
                        } else if (debug_level == DEBUG_SIMPLE || debug_level == DEBUG_FULL) {
                            // Log normal end-of-file events only if debug is enabled
                            UtilityFunctions::print("Playback ended with reason: ", end_file->reason);
                        }
                    }
                    break;
                    
                case MPV_EVENT_LOG_MESSAGE:
                    if(debug_level == DEBUG_FULL) {
                        mpv_event_log_message* msg = static_cast<mpv_event_log_message*>(event->data);
                        UtilityFunctions::print("MPV Log [", msg->prefix, "]: ", msg->text);
                    }
                    break;
                    
                case MPV_EVENT_PROPERTY_CHANGE:
                    if(is_streaming && debug_level == DEBUG_FULL) {
                        UtilityFunctions::print("Property changed");
                    }
                    break;
            }
            
            event = mpv_wait_event(mpv, 0);
        }
        
        // For streaming, we need to be more aggressive about requesting frame updates
        if (is_streaming) {
            // For HTTP streams, check playback status
            int64_t time_pos = 0;
            int paused = 0;
            
            // Get current playback position
            if (mpv_get_property(mpv, "time-pos", MPV_FORMAT_INT64, &time_pos) >= 0) {
                if (debug_level == DEBUG_FULL && frame_count % 60 == 0) {
                    UtilityFunctions::print("Stream position: ", time_pos, " seconds");
                }
            }
            
            // Check if playback is paused
            if (mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &paused) >= 0) {
                if (paused) {
                    // If paused, try to resume playback
                    if (debug_level == DEBUG_SIMPLE || debug_level == DEBUG_FULL) {
                        UtilityFunctions::print("Stream is paused, attempting to resume");
                    }
                    mpv_set_property_string(mpv, "pause", "no");
                }
            }
            
            // Force a frame update every few frames to keep the stream going
            if (frame_count % 10 == 0) {
                texture_needs_update.store(true);
                
                if(debug_level == DEBUG_FULL) {
                    UtilityFunctions::print("Requesting frame update for streaming, frame #", frame_count);
                }
                
                // If we haven't seen any content after a significant number of frames, try to restart
                if (!had_visible_content && frame_count > 100) {
                    if(debug_level == DEBUG_SIMPLE || debug_level == DEBUG_FULL) {
                        UtilityFunctions::print("No visible content after ", frame_count, " frames, trying to restart playback");
                    }
                    
                    // Send a seek command to restart playback
                    const char* cmd[] = {"seek", "0", "absolute", nullptr};
                    mpv_command_async(mpv, 0, cmd);
                    
                    // Reset frame counter
                    frame_count = 0;
                }
            }
        }
    }
}

void MPVPlayer::load_file(const String& path) {
    UtilityFunctions::print("Loading video: ", path);
    
    // Check if MPV is initialized
    if (!mpv) {
        UtilityFunctions::print("MPV is not initialized");
        return;
    }
    
    // Check if this is a streaming URL
    is_streaming = path.begins_with("http://") || path.begins_with("https://");
    
    // Reset frame counter and content flag when loading a new file/stream
    frame_count = 0;
    had_visible_content = false;
    
    if (is_streaming) {
        UtilityFunctions::print("Detected HTTP stream, enabling streaming mode");
        
        // Set streaming-specific options for MPV
        // These match closer to command-line mpv defaults
        mpv_set_option_string(mpv, "network-timeout", "60"); // 60 seconds timeout (default in mpv)
        mpv_set_option_string(mpv, "demuxer-readahead-secs", "20"); // Read ahead 20 seconds
        mpv_set_option_string(mpv, "cache", "yes"); // Enable cache
        mpv_set_option_string(mpv, "cache-secs", "30"); // Cache 30 seconds (more generous)
        mpv_set_option_string(mpv, "force-seekable", "yes"); // Try to make stream seekable
        
        // Set these to match command-line behavior
        mpv_set_option_string(mpv, "audio-file-auto", "no"); // Don't load external audio
        mpv_set_option_string(mpv, "sub-auto", "no"); // Don't load subtitles
        
        UtilityFunctions::print("Applied streaming-specific MPV options");
    } else {
        // Local file options
        mpv_set_option_string(mpv, "cache", "auto");
    }
    
    // Convert Godot String to C string - we need to keep the CharString alive
    // until after the command is executed to prevent the pointer from becoming invalid
    CharString cs = path.utf8();
    const char* c_path = cs.get_data();
    
    if (c_path == nullptr || c_path[0] == '\0') {
        UtilityFunctions::print("ERROR: Invalid empty path");
        return;
    }
    
    UtilityFunctions::print("Loading path: '", c_path, "'");
    
    // For HTTP streams, use synchronous command to match command-line behavior
    // This ensures the command completes before continuing
    if (is_streaming) {
        const char* cmd[] = {"loadfile", c_path, nullptr};
        int result = mpv_command(mpv, cmd);
        if (result < 0) {
            UtilityFunctions::print("Error loading stream: ", mpv_error_string(result));
        } else {
            UtilityFunctions::print("Stream loaded successfully");
        }
    } else {
        // For local files, use async command as before
        const char* cmd[] = {"loadfile", c_path, nullptr};
        mpv_command_async(mpv, 0, cmd);
    }
}

void MPVPlayer::play() {
    if (!mpv) {
        ERR_PRINT("MPV not initialized");
        return;
    }
    if(debug_level == DEBUG_SIMPLE || debug_level == DEBUG_FULL)
    UtilityFunctions::print("Starting playback");
    
    const char* cmd[] = {"set", "pause", "no", nullptr};
    mpv_command_async(mpv, 0, cmd);
}

void MPVPlayer::pause() {
    if (!mpv) {
        ERR_PRINT("MPV not initialized");
        return;
    }
    
    const char* cmd[] = {"set", "pause", "yes", nullptr};
    mpv_command_async(mpv, 0, cmd);
}

void MPVPlayer::stop() {
    if (!mpv) {
        ERR_PRINT("MPV not initialized");
        return;
    }
    
    const char* cmd[] = {"stop", nullptr};
    mpv_command_async(mpv, 0, cmd);
}

MeshInstance2D* MPVPlayer::create_video_mesh_2d() {
    // Create a new MeshInstance2D if it doesn't exist
    if (!video_mesh) {
        video_mesh = memnew(MeshInstance2D);
        
        // Create a simple quad mesh
        Ref<QuadMesh> quad_mesh;
        quad_mesh.instantiate();
        
        // Set the mesh size based on the video dimensions
        quad_mesh->set_size(Vector2(width, height));
        
        // Set the mesh on the MeshInstance2D
        video_mesh->set_mesh(quad_mesh);
        
        // Create a material for the mesh
        Ref<ShaderMaterial> material;
        material.instantiate();
        
        // Set the texture on the material
        if (frame_texture.is_valid()) {
            material->set_shader_parameter("texture", frame_texture);
        }
        
        // Set the material on the mesh
        video_mesh->set_material(material);
    }
    
    return video_mesh;
}

MeshInstance3D* MPVPlayer::create_video_mesh_3d() {
    // Create a new MeshInstance3D if it doesn't exist
    if (!video_mesh_3d) {
        video_mesh_3d = memnew(MeshInstance3D);
        
        // Create a plane mesh
        Ref<PlaneMesh> plane_mesh;
        plane_mesh.instantiate();
        
        // Set the mesh size based on the video dimensions
        float aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
        plane_mesh->set_size(Vector2(2.0f, 2.0f / aspect_ratio));
        
        // Set the mesh on the MeshInstance3D
        video_mesh_3d->set_mesh(plane_mesh);
        
        // Create a material for the mesh
        Ref<StandardMaterial3D> material;
        material.instantiate();
        
        // Configure the material
        material->set_roughness(1.0f);  // Non-reflective
        material->set_metallic(0.0f);   // Non-metallic
        material->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);  // No lighting effects
        
        // Set the texture on the material
        if (frame_texture.is_valid()) {
            material->set_texture(StandardMaterial3D::TEXTURE_ALBEDO, frame_texture);
        }
        
        // Set the material on the mesh
        video_mesh_3d->set_surface_override_material(0, material);
    }
    
    return video_mesh_3d;
}

void MPVPlayer::apply_to_mesh_3d(MeshInstance3D* mesh_instance) {
    if (mesh_instance) {
        // Get the material from the mesh
        Ref<Material> material = mesh_instance->get_surface_override_material(0);
        
        // If no material exists, create a new one
        if (!material.is_valid()) {
            Ref<StandardMaterial3D> new_material;
            new_material.instantiate();
            
            // Configure the material
            new_material->set_roughness(1.0f);  // Non-reflective
            new_material->set_metallic(0.0f);   // Non-metallic
            new_material->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);  // No lighting effects
            
            material = new_material;
            mesh_instance->set_surface_override_material(0, material);
        }
        
        // Update the material's texture
        if (frame_texture.is_valid()) {
            update_3d_material_texture(material, frame_texture);
        }
    }
}

void MPVPlayer::apply_to_viewport(SubViewport* viewport) {
    if (viewport && frame_texture.is_valid()) {
        // Set the viewport's texture to the frame texture
        // For now, we'll just print a message
        UtilityFunctions::print("Applying texture to viewport");
    }
}

Ref<Texture2D> MPVPlayer::get_texture() const {
    return frame_texture;
}

void MPVPlayer::update_3d_material_texture(Ref<Material> p_material, Ref<Texture2D> p_texture) {
    if (p_material.is_null() || p_texture.is_null()) {
        return;
    }
    
    // Try to cast to StandardMaterial3D
    StandardMaterial3D* std_mat = Object::cast_to<StandardMaterial3D>(p_material.ptr());
    if (std_mat) {
        // Use the correct method for StandardMaterial3D
        std_mat->set_texture(StandardMaterial3D::TEXTURE_ALBEDO, p_texture);
        return;
    }
    
    // If we get here, we couldn't update the material
    UtilityFunctions::print("Warning: Could not update 3D material texture");
}