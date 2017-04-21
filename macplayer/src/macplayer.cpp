#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

extern "C" {
#include <android/libon2/vpu_api.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libavutil/mem.h>
#include "bytestream.h"
}

#include "macplayer.h"
#include "macffplay.h"
#include "DVDVideoCodec.h"
#include "DVDVideoCodecHybris.h"
#include <hybris/media/media_compatibility_layer.h>
#include <hybris/surface_flinger/surface_flinger_compatibility_layer.h>
#include <hybris/input/input_stack_compatibility_layer.h>


//#define REPORT_FUNCTION() do { printf("R(%p): %s \n", pthread_self(), __PRETTY_FUNCTION__); fflush(stdout); } while(0)
#define REPORT_FUNCTION() 

EGLDisplay disp = NULL;
EGLSurface surface = NULL;

struct ClientWithSurface
{
	struct SfClient* client;
	struct SfSurface* surface;
};

struct ClientWithSurface cs;

GLuint textureID = 0;
int textureWidth = 1920;
int textureHeight = 1080;

#define DVD_NOPTS_VALUE    (-1LL<<52)

RK_S32 pkt_size =0;
VideoPacket_t videoPacket;
VideoPacket_t* pkt =NULL;
RK_S64 fakeTimeUs = 0;
RK_U8* pExtra = NULL;
RK_U32 extraSize = 0;

static int64_t guess_correct_pts(AVCodecContext *ctx,
                                 int64_t reordered_pts, int64_t dts)
{
    int64_t pts = AV_NOPTS_VALUE;

    if (dts != AV_NOPTS_VALUE) {
        ctx->pts_correction_num_faulty_dts += dts <= ctx->pts_correction_last_dts;
        ctx->pts_correction_last_dts = dts;
    } else if (reordered_pts != AV_NOPTS_VALUE)
        ctx->pts_correction_last_dts = reordered_pts;

    if (reordered_pts != AV_NOPTS_VALUE) {
        ctx->pts_correction_num_faulty_pts += reordered_pts <= ctx->pts_correction_last_pts;
        ctx->pts_correction_last_pts = reordered_pts;
    } else if(dts != AV_NOPTS_VALUE)
        ctx->pts_correction_last_pts = dts;

    if ((ctx->pts_correction_num_faulty_pts<=ctx->pts_correction_num_faulty_dts || dts == AV_NOPTS_VALUE)
       && reordered_pts != AV_NOPTS_VALUE)
        pts = reordered_pts;
    else
        pts = dts;

    return pts;
}

extern "C" int hybris_codec_decode(void* vpHybrisCodec, AVCodecContext *avctx, AVFrame *picture, int *got_picture_ptr, const AVPacket *avpkt) {
    REPORT_FUNCTION();

    int ret = 0;
    AVPacket packet;
    memcpy(&packet,avpkt,sizeof(AVPacket));

    CDVDVideoCodecHybris* pHybrisCodec = (CDVDVideoCodecHybris*)vpHybrisCodec; 
    *got_picture_ptr = 0;
    DVDVideoPicture hybrisPicture;
    av_frame_unref(picture);

    unsigned char *data = NULL;
    unsigned int pkt_size = 0;

    pkt->pts = VPU_API_NOPTS_VALUE;
    pkt->dts = VPU_API_NOPTS_VALUE;

    data = packet.data;
    pkt_size = packet.size;

    if (pkt->data ==NULL) {
        pkt->data = (RK_U8*)(malloc)(pkt_size);
        if (pkt->data ==NULL) {
            ERROR("malloc() error\n"); return -1;
        }
        pkt->capability = pkt_size;
    }
    if (pkt->capability <((RK_U32)pkt_size)) {
        pkt->data = (RK_U8*)(realloc)((void*)(pkt->data), pkt_size);
        if (pkt->data ==NULL) {
            ERROR("realloc() error\n"); return -1;
        }
        pkt->capability = pkt_size;
    }
    memcpy(pkt->data,data,pkt_size);
    pkt->size = pkt_size;
    pkt->dts = packet.dts;
    pkt->pts = packet.pts;
    
    do {
        ret = pHybrisCodec->Decode( pkt->data,pkt->size, 
				    packet.dts, packet.pts );
                                    ///DVD_NOPTS_VALUE, DVD_NOPTS_VALUE );

        if ( ret & VC_PICTURE ) {
            *got_picture_ptr = 1;

            pHybrisCodec->ClearPicture(&hybrisPicture);
            pHybrisCodec->GetPicture(&hybrisPicture);

            picture->pkt_dts = packet.dts;
            picture->pkt_pts = packet.pts;
            if(hybrisPicture.pts != DVD_NOPTS_VALUE )
                picture->pkt_pts = hybrisPicture.pts;

            if(!avctx->has_b_frames){
                av_frame_set_pkt_pos(picture, avpkt->pos);
            }
            if (!picture->sample_aspect_ratio.num) 
                picture->sample_aspect_ratio = avctx->sample_aspect_ratio;
            if (!picture->width) 
                picture->width = avctx->width;
            if (!picture->height) 
                picture->height = avctx->height;
            if (picture->format == AV_PIX_FMT_NONE) 
                picture->format = avctx->pix_fmt;
   
            avctx->frame_number++;
            //av_frame_set_best_effort_timestamp(picture,
                //guess_correct_pts(avctx, picture->pkt_pts, picture->pkt_dts));

            hybrisPicture.mediacodec->Retain();
            hybrisPicture.mediacodec->ReleaseOutputBuffer(true);

            g_displayHandler->obtainMessage(TEXTURE_RENDERED,(int32_t)hybrisPicture.mediacodec,0)->sendToTarget();
            hybrisPicture.mediacodec = NULL;
        }
    } while(0);

    ret = avpkt->size;
    return ret;
} 

extern "C" void* hybris_codec_open(enum AVCodecID codec_id, int width, int height, uint8_t*  pExtraData, int extraDataSize, int profile, int ptsinvalid )
{
    REPORT_FUNCTION();

    CDVDStreamInfo hints;
    hints.codec = codec_id;
    hints.width = width;
    hints.height = height;
    hints.extradata = pExtraData;
    hints.extrasize = extraSize;
    hints.profile = 0; // unused yet
    hints.ptsinvalid = true;
    CDVDVideoCodecHybris* pHybrisCodec = new CDVDVideoCodecHybris();
    if( !pHybrisCodec->Open(hints, pExtraData,extraDataSize) ) {
        ERROR("Can't open hybris codec");
        return NULL;
    }

    memset(&videoPacket, 0, sizeof(VideoPacket_t));
    pkt = &videoPacket;
    pkt->data = NULL;
    pkt->pts = VPU_API_NOPTS_VALUE;
    pkt->dts = VPU_API_NOPTS_VALUE;

    /// texture has been created
    textureID = pHybrisCodec->GetTextureID();
    textureWidth = width;
    textureHeight = height;

    g_displayHandler->obtainMessage(VIDEO_RESIZED,width,height)->sendToTarget();

    return (void*)pHybrisCodec;
}

extern "C" void hybris_codec_close( void* vpHybrisCodec ) {
    REPORT_FUNCTION();

    CDVDVideoCodecHybris* pHybrisCodec = (CDVDVideoCodecHybris*)vpHybrisCodec;
    if( pHybrisCodec ) {
        delete pHybrisCodec;
        pHybrisCodec = NULL;
    }
}

//////////////////////////////////////////////////////////////////////////

#include <hybris/media/media_compatibility_layer.h>
#include <hybris/surface_flinger/surface_flinger_compatibility_layer.h>

#define GLES_VERSION 2 
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "Matrix.h"
#include "Mathematics.h"

using namespace MaliSDK;

static float DestWidth = 0.0, DestHeight = 0.0;
static int iDestWidth = 0, iDestHeight = 0;
static int Width = 0, Height = 0; // Actual video dimmensions

/* Texture variables. */
///GLuint textureID = 0;

/* Shader variables. */
GLuint programID = 0;
GLint iLocTextureMatrix = -1;
GLint iLocPosition = -1;
GLint iLocTextureMix = -1;
GLint iLocTexture = -1;
GLint iLocTexCoord = -1;

/* Animation variables. */
Matrix translation;
Matrix scale;
Matrix negativeTranslation;

int windowWidth = -1;
int windowHeight = -1;

/* These indices describe the quad triangle strip. */
static const GLubyte quadIndices[] = { 0, 1, 2, 3, };

/* Tri strips, so quad is in this order:
 *
 * 2 ----- 3
 * | \     |
 * |   \   |
 * |     \ |
 * 0 ----- 1
 */
static const float quadVertices[] = {
    /* Front. */
    -1.0f, -1.0f,  0.0f, /* 0 */
     1.0f, -1.0f,  0.0f, /* 1 */
    -1.0f,  1.0f,  0.0f, /* 2 */
     1.0f,  1.0f,  0.0f, /* 3 */
};

static const float quadTextureCoordinates[] = {
    /* Front. */
    0.0f, 1.0f, /* 0 */
    1.0f, 1.0f, /* 1 */
    0.0f, 0.0f, /* 2 */
    1.0f, 0.0f, /* 3 */ 
    /* Flipped Y coords. */
};

#define GL_CHECK(x) \
    x; \
    { \
        GLenum glError = glGetError(); \
        if(glError != GL_NO_ERROR) { \
            printf("glGetError() = %i (0x%.8x) at %s:%i\n", glError, glError, __FILE__, __LINE__); \
            exit(1); \
        } \
    }

static GLuint gProgram;
static GLuint gaPositionHandle, gaTexHandle, gsTextureHandle, gmTexMatrix;

static GLfloat positionCoordinates[8];

///struct MediaPlayerWrapper *player = NULL;

static float mtxIdentity[16] = {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1,
};

void calculate_position_coordinates()
{
        // Assuming cropping output for now
        float x = 1, y = 1;

        // Black borders
        x = (float) (textureWidth / DestWidth);
        y = (float) (textureHeight / DestHeight);

        // Make the larger side be 1
        if (x > y) {
                y /= x;
                x = 1;
        } else {
                x /= y;
                y = 1;
        }

        positionCoordinates[0] = -x;
        positionCoordinates[1] = y;
        positionCoordinates[2] = -x;
        positionCoordinates[3] = -y;
        positionCoordinates[4] = x;
        positionCoordinates[5] = -y;
        positionCoordinates[6] = x;
        positionCoordinates[7] = y;

/*
        positionCoordinates[1] = -y;
        positionCoordinates[3] = y;
        positionCoordinates[5] = y;
        positionCoordinates[7] = -y;
*/
}

struct ClientWithSurface client_with_surface(bool setup_surface_with_egl) {
	struct ClientWithSurface cs;
	cs.client = sf_client_create();
	if (!cs.client) {
		printf("Problem creating client ... aborting now.");
		return cs;
	}

	static const size_t primary_display = 0;

	DestWidth = sf_get_display_width(primary_display);
	DestHeight = sf_get_display_height(primary_display);
        iDestWidth = (int)DestWidth;
        iDestHeight = (int)DestHeight;
	printf("Primary display width: %f, height: %f\n", 
		DestWidth, DestHeight);

	SfSurfaceCreationParameters params = { 0, 0, iDestWidth, iDestHeight,
		-1, 15000, 1.0f,
		setup_surface_with_egl, "MACSurface"
	};

	cs.surface = sf_surface_create(cs.client, &params);
	if (!cs.surface) {
		printf("Problem creating surface ... aborting now.");
		return cs;
	}

	sf_surface_make_current(cs.surface);
	return cs;
}

#if 0
static const char *vertex_shader()
{
	return

"attribute vec4 a_v4Position;						\n"
"attribute vec2 a_v2TexCoord;						\n"
"uniform mat4 u_m4Texture;						\n"
"varying vec2 v_v2TexCoord;						\n"
"void main()								\n"
"{									\n"
"    v_v2TexCoord = vec2(u_m4Texture * vec4(a_v2TexCoord, 0.0, 1.0));;	\n"
"    gl_Position = a_v4Position;					\n"
"}									\n";

}

static const char *fragment_shader()
{
	return

"#extension GL_OES_EGL_image_external : require	      		\n"
"precision mediump float;					\n"
"uniform samplerExternalOES u_s2dTexture;			\n"
"varying vec2 v_v2TexCoord;					\n"
"void main()							\n"
"{								\n"
"   vec4 v4Texel = texture2D(u_s2dTexture, v_v2TexCoord);	\n"
"   gl_FragColor = v4Texel;					\n"
"}								\n";

}
#endif

static const char *vertex_shader()
{
        return
                "attribute vec4 a_position;                                  \n"
                "attribute vec2 a_texCoord;                                  \n"
                "uniform mat4 m_texMatrix;                                   \n"
                "varying vec2 v_texCoord;                                    \n"
                "varying float topDown;                                      \n"
                "void main()                                                 \n"
                "{                                                           \n"
                "   gl_Position = a_position;                                \n"
                "   v_texCoord = (m_texMatrix * vec4(a_texCoord, 0.0, 1.0)).xy;\n"
                "}                                                           \n";
}

static const char *fragment_shader()
{
        return
                "#extension GL_OES_EGL_image_external : require      \n"
                "precision mediump float;                            \n"
                "varying vec2 v_texCoord;                            \n"
                "uniform samplerExternalOES s_texture;               \n"
                "void main()                                         \n"
                "{                                                   \n"
                "  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
                "}                                                   \n";
}

static GLuint loadShader(GLenum shaderType, const char* pSource) {
	GLuint shader = glCreateShader(shaderType);

	if (shader) {
		glShaderSource(shader, 1, &pSource, NULL);
		glCompileShader(shader);
		GLint compiled = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

		if (!compiled) {
			GLint infoLen = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
			if (infoLen) {
				char* buf = (char*) malloc(infoLen);
				if (buf) {
					glGetShaderInfoLog(shader, infoLen, NULL, buf);
					fprintf(stderr, "Could not compile shader %d:\n%s\n",
							shaderType, buf);
					free(buf);
				}
				glDeleteShader(shader);
				shader = 0;
			}
		}
	} else {
		printf("Error, during shader creation: %i\n", glGetError());
	}

	return shader;
}

static GLuint create_program(const char* pVertexSource, const char* pFragmentSource) {
	GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
	if (!vertexShader) {
		printf("vertex shader not compiled\n");
		return 0;
	}

	GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
	if (!pixelShader) {
		printf("frag shader not compiled\n");
		return 0;
	}

	GLuint program = glCreateProgram();
	if (program) {
		glAttachShader(program, vertexShader);
		glAttachShader(program, pixelShader);
		glLinkProgram(program);

		GLint linkStatus = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
		if (linkStatus != GL_TRUE) {
			GLint bufLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
			if (bufLength) {
				char* buf = (char*) malloc(bufLength);
				if (buf) {
					glGetProgramInfoLog(program, bufLength, NULL, buf);
					fprintf(stderr, "Could not link program:\n%s\n", buf);
					free(buf);
				}
			}
			glDeleteProgram(program);
			program = 0;
		}

		glUseProgram(program);
	}

	return program;
}

int setupGraphics(struct ClientWithSurface *cs,int width, int height )
{
    ///int textureWidth = 1920;
    ///int textureHeight = 1080;
 
    windowWidth = width;
    windowHeight = height;
    INFO("windowWidth=%d",windowWidth);
    INFO("windowHeight=%d",windowHeight);

    /* Make scale matrix to centre texture on screen. */
    translation = Matrix::createTranslation(0.5f, 0.5f, 0.0f);
    scale = Matrix::createScaling(windowWidth / (float)textureWidth, windowHeight / (float)textureHeight, 1.0f); /* 2.0 makes it smaller, 0.5 makes it bigger. */
    negativeTranslation = Matrix::createTranslation(-0.5f, -0.5f, 0.0f);

    /* Initialize OpenGL ES. */
    GL_CHECK(glEnable(GL_CULL_FACE));
    GL_CHECK(glCullFace(GL_BACK));
    GL_CHECK(glEnable(GL_DEPTH_TEST));
    GL_CHECK(glEnable(GL_BLEND));
    /* Should do src * (src alpha) + dest * (1-src alpha). */
    GL_CHECK(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    programID = create_program(vertex_shader(), fragment_shader());

    iLocPosition = GL_CHECK(glGetAttribLocation(programID, "a_v4Position"));
    if(iLocPosition == -1) {
        printf("Attribute not found at %s:%i\n", __FILE__, __LINE__);
        return -1;
    }
    GL_CHECK(glEnableVertexAttribArray(iLocPosition));

    iLocTexture = GL_CHECK(glGetUniformLocation(programID, "u_s2dTexture"));
    if(iLocTexture == -1) {
        printf("Warning: Uniform not found at %s:%i\n", __FILE__, __LINE__);
    } else {
        GL_CHECK(glUniform1i(iLocTexture, 0));
    }

    iLocTexCoord = GL_CHECK(glGetAttribLocation(programID, "a_v2TexCoord"));
    if(iLocTexCoord == -1) {
        printf("Warning: Attribute not found at %s:%i\n", __FILE__, __LINE__);
    } else {
        GL_CHECK(glEnableVertexAttribArray(iLocTexCoord));
    }

    iLocTextureMatrix = GL_CHECK(glGetUniformLocation(programID, "u_m4Texture"));
    if(iLocTextureMatrix == -1) {
        printf("Warning: Uniform not found at %s:%i\n", __FILE__, __LINE__);
    } else {
        GL_CHECK(glUniformMatrix4fv(iLocTextureMatrix, 1, GL_FALSE, scale.getAsArray()));
    }

    return 0;
}

void renderFrame()
{
    static float angleZTexture = 0.0f;
    static float angleZOffset = 0.0f;
    static float angleZoom = 0.0f;
    static Vec4f radius = {0.0f, 1.0f, 0.0f, 1.0f};

    GL_CHECK(glUseProgram(programID));

    GL_CHECK(glEnableVertexAttribArray(iLocPosition));
    GL_CHECK(glVertexAttribPointer(iLocPosition, 3, GL_FLOAT, GL_FALSE, 0, quadVertices));

    if(iLocTexCoord != -1) {
        GL_CHECK(glEnableVertexAttribArray(iLocTexCoord));
        GL_CHECK(glVertexAttribPointer(iLocTexCoord, 2, GL_FLOAT, GL_FALSE, 0, quadTextureCoordinates));
    }

    ///int borderW = (windowWidth-((windowWidth/textureWidth)*textureWidth))/2;
    int borderW = ((16-(textureWidth%16))*(windowWidth/textureWidth))/2;
    ///int borderH = (windowHeight-((windowHeight/textureHeight)*textureHeight))/2;
    int borderH = 0;
    GL_CHECK(glViewport(borderW, borderH, windowWidth, windowHeight));
    ///GL_CHECK(glViewport(0, 0, windowWidth, windowHeight));

    GL_CHECK(glClearColor(1.0f, 1.0f, 0.0f, 1.0));
    GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

    /* Construct a rotation matrix for rotating the texture about its centre. */
    Matrix rotateTextureZ = Matrix::createRotationZ(angleZTexture);
    Matrix rotateOffsetZ = Matrix::createRotationZ(angleZOffset);
    Vec4f offset = Matrix::vertexTransform(&radius, &rotateOffsetZ);

    /* Construct offset translation. */
    Matrix translateTexture = Matrix::createTranslation(offset.x, offset.y, offset.z);

    /* Construct zoom matrix. */
    Matrix zoom = Matrix::createScaling(sinf(degreesToRadians(angleZoom)) * 0.75f + 1.25f, sinf(degreesToRadians(angleZoom)) * 0.75f + 1.25f, 1.0f);

    /* Create texture matrix. Operations happen bottom-up order. */
    Matrix textureMovement = Matrix::identityMatrix * translation; /* Translate texture back to original position. */
    ///textureMovement = textureMovement * rotateTextureZ;            /* Rotate texture about origin. */
    ///textureMovement = textureMovement * translateTexture;          /* Translate texture away from origin. */
    ///textureMovement = textureMovement * zoom;                      /* Zoom the texture. */
    
    float xScale = windowWidth / (float)textureWidth;
    float yScale = windowHeight / (float)textureHeight;
    ///float fZoom = xScale > yScale ? xScale : yScale; 
    float fZoom = xScale > yScale ? yScale : xScale; 
    scale = Matrix::createScaling(windowWidth / (float)textureWidth, windowHeight / (float)textureHeight, fZoom); /* 2.0 makes it smaller, 0.5 makes it bigger. */
    ///scale = Matrix::createScaling((float)textureWidth / (float)windowWidth, (float)textureHeight / (float)windowHeight , 1.0f); /* 2.0 makes it smaller, 0.5 makes it bigger. */
    ///textureMovement = textureMovement * scale;                     /* Scale texture down in size from fullscreen to 1:1. */
    textureMovement = textureMovement * negativeTranslation;       /* Translate texture to be centred on origin. */

    GL_CHECK(glUniformMatrix4fv(iLocTextureMatrix, 1, GL_FALSE, textureMovement.getAsArray()));

    /* Ensure the correct texture is bound to texture unit 0. */
    GL_CHECK(glActiveTexture(GL_TEXTURE0));
    GL_CHECK(glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureID));

    /* And draw. */
    GL_CHECK(glDrawElements(GL_TRIANGLE_STRIP, sizeof(quadIndices) / sizeof(GLubyte), GL_UNSIGNED_BYTE, quadIndices));

    /* Update rotation angles for animating. */
    angleZTexture += 1;
    angleZOffset += 1;
    angleZoom += 1;

    if(angleZTexture >= 360) angleZTexture -= 360;
    if(angleZTexture < 0) angleZTexture += 360;

    if(angleZOffset >= 360) angleZOffset -= 360;
    if(angleZOffset < 0) angleZOffset += 360;

    if(angleZoom >= 360) angleZoom -= 360;
    if(angleZoom < 0) angleZoom += 360;
}

int setupGraphics2(struct ClientWithSurface *cs,int width, int height ) {
    windowWidth = width;
    windowHeight = height;
    INFO("windowWidth=%d",windowWidth);
    INFO("windowHeight=%d",windowHeight);

    gProgram = create_program(vertex_shader(), fragment_shader());
    gaPositionHandle = glGetAttribLocation(gProgram, "a_position");
    gaTexHandle = glGetAttribLocation(gProgram, "a_texCoord");
    gsTextureHandle = glGetUniformLocation(gProgram, "s_texture");
    gmTexMatrix = glGetUniformLocation(gProgram, "m_texMatrix");

    return 0;
}

GLfloat matrix[16];

void renderFrame2() {
                GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

                const GLfloat textureCoordinates[] = {
                        0.0f,  1.0f,
                        0.0f,  0.0f,
                        1.0f,  0.0f,
                        1.0f,  1.0f
                };

                ///android_media_update_surface_texture(player);

                calculate_position_coordinates();

                //Clear(GL_COLOR_BUFFER_BIT);

                // Use the program object
                glUseProgram(gProgram);
                // Enable attributes
                glEnableVertexAttribArray(gaPositionHandle);
                glEnableVertexAttribArray(gaTexHandle);
                // Load the vertex position
                glVertexAttribPointer(gaPositionHandle,
                                2,
                                GL_FLOAT,
                                GL_FALSE,
                                0,
                                positionCoordinates);
                // Load the texture coordinate
                glVertexAttribPointer(gaTexHandle,
                                2,
                                GL_FLOAT,
                                GL_FALSE,
                                0,
                                textureCoordinates);

                ///GLfloat matrix[16];
                ///android_media_surface_texture_get_transformation_matrix(player, matrix);
                ///memcpy(matrix,mtxIdentity,sizeof(matrix)); /// id-matrix

                glUniformMatrix4fv(gmTexMatrix, 1, GL_FALSE, matrix);

                glActiveTexture(GL_TEXTURE0);
                // Set the sampler texture unit to 0
                glUniform1i(gsTextureHandle, 0);
                //glUniform1i(gmTexMatrix, 0);
                ///android_media_update_surface_texture(player);

                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
                //glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
                glDisableVertexAttribArray(gaPositionHandle);
                glDisableVertexAttribArray(gaTexHandle);
}

void init_render() {
	printf("Creating EGL surface.\n");
        cs = client_with_surface(true );
	if (!cs.surface) {
		printf("Problem acquiring surface\n");
		return ;
	}

	printf("Creating GL texture.\n");
	disp = sf_client_get_egl_display(cs.client);
	surface = sf_surface_get_egl_surface(cs.surface);

	sf_surface_make_current(cs.surface);

	if (setupGraphics2(&cs, (int)DestWidth, (int)DestHeight )!=0) {
		printf("Problem setting up texture for surface.\n");
		return ;
	}
}

void step_render() {
            renderFrame2();
            eglSwapBuffers(disp, surface);
}

void update_render( CDVDMediaCodecInfo* mediacodec ) {
        mediacodec->UpdateTexImage();
        mediacodec->GetTransformMatrix(matrix);
}

//////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <fcntl.h>
#include <termios.h>
#define NB_ENABLE 0
#define NB_DISABLE 1

void advance_cursor() {
  static int pos=0;
  char cursor[4]={'/','-','\\','|'};
  printf("%c\b", cursor[pos]);
  fflush(stdout);
  pos = (pos+1) % 4;
}

int kbhit()
{
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0
    select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}

void nonblock(int state)
{
    struct termios ttystate;
 
    //get the terminal state
    tcgetattr(STDIN_FILENO, &ttystate);
 
    if (state==NB_ENABLE)
    {
        //turn off canonical mode
        ttystate.c_lflag &= ~ICANON;
        //minimum of number input read.
        ttystate.c_cc[VMIN] = 1;
    }
    else if (state==NB_DISABLE)
    {
        //turn on canonical mode
        ttystate.c_lflag |= ICANON;
    }
    //set the terminal attributes.
    tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
}
	
void setSchedulingParams( int priority ) {
    int policy = DEFAULT_SCHED;

    int max = sched_get_priority_max(policy);
    int min = sched_get_priority_min(policy);

    sched_param schedulingParameters;
    memset(&schedulingParameters, 0, sizeof(schedulingParameters));
    schedulingParameters.sched_priority = priority;
    pthread_setschedparam(pthread_self(), policy, &schedulingParameters);

    memset(&schedulingParameters, 0, sizeof(schedulingParameters));
    policy = 0;
    pthread_getschedparam(pthread_self(), &policy, &schedulingParameters);

    INFO("thead(%p): policy %s, prio %d [%d-%d]", pthread_self(), 
         policy == SCHED_FIFO ? "FIFO" : (policy == SCHED_RR ? "RR" : "OTHER"), 
         schedulingParameters.sched_priority, min, max );
}

void printSchedulingParams( void ) {
    int policy = 0;
    int max = sched_get_priority_max(policy);
    int min = sched_get_priority_min(policy);

    sched_param schedulingParameters;
    memset(&schedulingParameters, 0, sizeof(schedulingParameters));
    pthread_getschedparam(pthread_self(), &policy, &schedulingParameters);

    INFO("thead(%p): policy %s, prio %d [%d-%d]", pthread_self(), 
         policy == SCHED_FIFO ? "FIFO" : (policy == SCHED_RR ? "RR" : "OTHER"), 
         schedulingParameters.sched_priority, min, max );
}

void on_new_event(struct Event* event, void* context) {
        printf("\tEventType: %d \n", event->type);
        printf("\tdevice_id: %d \n", event->device_id);
        printf("\tsource_id: %d \n", event->source_id);
        printf("\taction: %d \n", event->action);
        printf("\tflags: %d \n", event->flags);
        printf("\tmeta_state: %d \n", event->meta_state);

        switch (event->type) {
        case MOTION_EVENT_TYPE:
                printf("\tdetails.motion.event_time: %lld\n",
                                event->details.motion.event_time);
                printf("\tdetails.motion.pointer_coords.x: %f\n",
                                event->details.motion.pointer_coordinates[0].x);
                printf("\tdetails.motion.pointer_coords.y: %f\n",
                                event->details.motion.pointer_coordinates[0].y);
                break;
        case KEY_EVENT_TYPE:
                printf("\tdetails.motion.key.event_time: %lld\n",
                                event->details.key.event_time);
                printf("\tdetails.motion.key.key_code: %d\n",
                                event->details.key.key_code);
                printf("\tdetails.motion.key.scan_code: %d\n",
                                event->details.key.scan_code);
                printf("\tdetails.motion.key.down_time: %lld\n",
                                event->details.key.down_time);

                macffplay_quit();
                break;
        default:
                break;
        }
}

sp<Handler> g_mainHandler;
sp<Handler> g_displayHandler;
void* g_mainthread = NULL;

sp<Thread> g_decoderThread = NULL;

///char* argv[4] = { "mac-l1", "-v", "32", "sintel_trailer-1080p.mp4" };
int mac_argc = 0;
char** mac_argv;

class DecoderThread : public Thread {
    virtual void run() {
        INFO("DecoderThread START");

        printSchedulingParams();

        macffplay(mac_argc, mac_argv, iDestWidth, iDestHeight );

        g_mainHandler->obtainMessage(DECODING_DONE)->sendToTarget();

        INFO("DecoderThread END");
    }
};

class StdinThread : public Thread {
    virtual void run() {
        INFO("StdinThread START");

        while(1) {
            unsigned char key = (unsigned char)fgetc(stdin);
            INFO("KEY = '%c' = %x", key, key);
            macffplay_key(key);
        }

        INFO("StdinThread END");
    }
};

class MainHandler : public Handler {
public:
    virtual void handleMessage(const sp<Message>& msg) {
        ThreadMessageCallback* callback = NULL;
        switch (msg->what) {
        case TMSG_CALLBACK:
            callback = (ThreadMessageCallback*)msg->arg1;
            callback->callback(callback->userptr);
            free(callback);

            // first TIC to do first draw 
            g_displayHandler->obtainMessage(RENDER_DISPLAY)->sendToTarget();
            break;

        case DECODERTHREAD_START:
            g_decoderThread->start();
            break;

        case DECODING_DONE:
            Looper::myLooper()->quit();
            break;
        }
    }
};

class DisplayHandler : public Handler {
public:
    virtual void handleMessage(const sp<Message>& msg) {
        CDVDMediaCodecInfo* mediacodec = NULL;

        switch (msg->what) {
        case VIDEO_RESIZED:
            textureWidth = msg->arg1;
            textureHeight = msg->arg2;
            INFO("VIDEO_RESIZED (%dx%d)!!!", textureWidth, textureHeight);
            break;

        case RENDER_DISPLAY:
            advance_cursor();
            step_render();
            break;

        case TEXTURE_RENDERED:
            mediacodec = (CDVDMediaCodecInfo*)msg->arg1;
            update_render( mediacodec );
            SAFE_RELEASE(mediacodec);

            this->obtainMessage(RENDER_DISPLAY)->sendToTarget();
            break;

        case KEY_INPUT:
            if(kbhit()!=0) {
                unsigned char key = (unsigned char)fgetc(stdin);
                INFO("KEY = '%c' = %x", key, key);
                macffplay_key(key);
            }

            this->sendMessageDelayed(this->obtainMessage(KEY_INPUT),100);
            break;
        }
    }
};

int main(int argc, char *argv[]) {
    mac_argc = argc;
    mac_argv = argv;

    setSchedulingParams(PRIO_DISPLAY);
    g_mainthread = (void*)pthread_self();
    Looper::prepare();

    init_render();

    sp<Handler> mainHandler = new MainHandler();
    sp<Handler> displayHandler = new DisplayHandler();
    g_mainHandler = mainHandler;
    g_displayHandler = displayHandler;

    sp<DecoderThread> decoderThread = new DecoderThread();
    g_decoderThread = decoderThread;
    g_decoderThread->setSchedulingParams(DEFAULT_SCHED, PRIO_VIDEOUPDATE);

    g_mainHandler->obtainMessage(DECODERTHREAD_START)->sendToTarget();
    g_displayHandler->obtainMessage(RENDER_DISPLAY)->sendToTarget();
    ///g_displayHandler->obtainMessage(KEY_INPUT)->sendToTarget();

    struct AndroidEventListener listener;
    listener.on_new_event = on_new_event;
    listener.context = NULL;

    struct InputStackConfiguration config = {
            enable_touch_point_visualization : true,
            default_layer_for_touch_point_visualization : 10000,
            input_area_width : iDestWidth,
            input_area_height : iDestHeight
    };

    android_input_stack_initialize(&listener, &config);
    android_input_stack_start();

    sp<StdinThread> stdinThread = new StdinThread();
    stdinThread->start();

    ///nonblock(NB_ENABLE);
    Looper::loop();
    ///nonblock(NB_DISABLE);

    android_input_stack_stop();
    android_input_stack_shutdown();

    return 0;
}
