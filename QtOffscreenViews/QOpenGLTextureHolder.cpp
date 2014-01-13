/*
  Offscreen Android Views library for Qt

  Author:
  Sergey A. Galin <sergey.galin@gmail.com>

  Distrbuted under The BSD License

  Copyright (c) 2014, DoubleGIS, LLC.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
  * Neither the name of the DoubleGIS, LLC nor the names of its contributors
    may be used to endorse or promote products derived from this software
    without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
  BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
  THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "QOpenGLTextureHolder.h"

static const GLuint c_vertex_coordinates_attr  = 0;
static const GLuint c_texture_coordinates_attr = 1;

QMap<GLenum, QSharedPointer<QGLShaderProgram> > QOpenGLTextureHolder::blit_programs_;

QOpenGLTextureHolder::QOpenGLTextureHolder(GLenum type, const QSize & size)
	: texture_id_(0)
	, texture_type_(type)
	, texture_size_(size)
	, a11_(1.0f), a12_(0), a21_(0), a22_(1.0f), b1_(0), b2_(0)
{
	Q_ASSERT(type == GL_TEXTURE_EXTERNAL_OES || type == GL_TEXTURE_2D);
	allocateTexture();
}

QOpenGLTextureHolder::QOpenGLTextureHolder()
	: texture_id_(0)
	, texture_type_(GL_TEXTURE_2D)
	, texture_size_(64, 64)
	, a11_(1.0f), a12_(0), a21_(0), a22_(1.0f), b1_(0), b2_(0)
{
}

QOpenGLTextureHolder::~QOpenGLTextureHolder()
{
	deallocateTexture();
}

void QOpenGLTextureHolder::deallocateTexture()
{
	if (texture_id_ != 0)
	{
		glDeleteTextures(1, &texture_id_);
		texture_id_ = 0;
	}
}

static inline void QRectFToVertexArray(const QRectF & r, GLfloat * array)
{
	qreal left = r.left();
	qreal right = r.right();
	qreal top = r.top();
	qreal bottom = r.bottom();
	array[0] = left;
	array[1] = top;
	array[2] = right;
	array[3] = top;
	array[4] = right;
	array[5] = bottom;
	array[6] = left;
	array[7] = bottom;
}

QGLShaderProgram * QOpenGLTextureHolder::GetBlitProgram(GLenum target)
{
	#if !defined(QT_OPENGL_ES_2)
		qFatal("GetBlitProgram should not be called if not using OpenGL ES 2.");
		Q_UNUSED(target);
		return 0;
	#else
		QSharedPointer<QGLShaderProgram> & blit_program_ptr = blit_programs_[target];
		if (!blit_program_ptr.isNull())
		{
			return blit_program_ptr.data();
		}

		qDebug()<<"Creating blit shaders for tetxure type"<<target;

		static const QLatin1String qglslMainWithTexCoordsVertexShader(
			"attribute highp vec2 textureCoordArray; \n"
			"varying highp vec2 textureCoords; \n"
			"void setPosition(); \n"
			"void main(void) \n"
			"{ \n"
			"  setPosition(); \n"
			"  textureCoords = textureCoordArray; \n"
			"}\n");

		static const QLatin1String qglslUntransformedPositionVertexShader(
			"attribute highp vec4 vertexCoordsArray; \n"
			"void setPosition(void) \n"
			"{ \n"
			"  gl_Position = vertexCoordsArray; \n"
			"}\n");

		static const QLatin1String qglslMainFragmentShader(
			"lowp vec4 srcPixel(); \n"
			"void main() \n"
			"{ \n"
			"  gl_FragColor = srcPixel(); \n"
			"}\n");

		QString qglslImageSrcFragmentShader;
		if (target == GL_TEXTURE_EXTERNAL_OES)
		{
			qglslImageSrcFragmentShader = QLatin1String(
				"#extension GL_OES_EGL_image_external : require \n"
				"varying highp vec2 textureCoords; \n"
				"uniform samplerExternalOES imageTexture; \n"
				"lowp vec4 srcPixel() \n"
				"{ \n"
				"  return texture2D(imageTexture, textureCoords); \n"
				"}\n");
		}
		else // expecting GL_TEXTURE_2D
		{
			Q_ASSERT(target == GL_TEXTURE_2D);
			qglslImageSrcFragmentShader = QLatin1String(
				"varying highp vec2 textureCoords; \n"
				"uniform sampler2D imageTexture; \n"
				"lowp vec4 srcPixel() \n"
				"{ \n"
				"  return texture2D(imageTexture, textureCoords); \n"
				"}\n");
		}

		blit_program_ptr = QSharedPointer<QGLShaderProgram>(new QGLShaderProgram());
		{
			QString source;
			source.append(qglslMainWithTexCoordsVertexShader);
			source.append(qglslUntransformedPositionVertexShader);

			QGLShader * vertexShader = new QGLShader(QGLShader::Vertex, blit_program_ptr.data());
			vertexShader->compileSourceCode(source);

			blit_program_ptr->addShader(vertexShader);
		}

		{
			QString source;
			// Fragment shader may contain #extension directive, and it should be before any other code,
			// as some drivers won't compile a shader with #extension in a middle.
			source.append(qglslImageSrcFragmentShader);
			source.append(qglslMainFragmentShader);

			QGLShader * fragmentShader = new QGLShader(QGLShader::Fragment, blit_program_ptr.data());
			fragmentShader->compileSourceCode(source);

			blit_program_ptr->addShader(fragmentShader);
		}

		blit_program_ptr->bindAttributeLocation("vertexCoordsArray", c_vertex_coordinates_attr);
		blit_program_ptr->bindAttributeLocation("textureCoordArray", c_texture_coordinates_attr);

		blit_program_ptr->link();

		return blit_program_ptr.data();
	#endif // ES 2.0
}

void QOpenGLTextureHolder::blitTexture(const QRect & targetRect, const QRect & sourceRect)
{
	if (!isAllocated())
	{
		qWarning()<<"Attempt to blit texture unallocated texture!";
		return;
	}
	if (targetRect.isEmpty() || sourceRect.isEmpty())
	{
		qWarning()<<"Attempt to blit texture with empty source or target rectangle!";
		return;
	}

	#if defined(QT_OPENGL_ES_2)
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_DEPTH_TEST);
		// glDisable(GL_SCISSOR_TEST);
		glDisable(GL_BLEND);

		QGLShaderProgram * blitProgram = GetBlitProgram(texture_type_);
		if (!blitProgram || !blitProgram->isLinked())
		{
			qWarning()<<"Shader program is not linked, can't blit the texture.";
			return;
		}
		if (!blitProgram->bind())
		{
			qWarning()<<"Failed to bind shader program, can't blit the texture.";
			return;
		}
		blitProgram->setUniformValue("imageTexture", 0 /*QT_IMAGE_TEXTURE_UNIT*/);

		// The shader manager's blit program does not multiply the
		// vertices by the pmv matrix, so we need to do the effect
		// of the orthographic projection here ourselves.
		QRectF r;
		qreal w = targetRect.width();
		qreal h = targetRect.height();
		r.setLeft((targetRect.left() / w) * 2.0f - 1.0f);
		if (targetRect.right() == (targetRect.width() - 1))
		{
			r.setRight(1.0f);
		}
		else
		{
			r.setRight((targetRect.right() / w) * 2.0f - 1.0f);
		}
		r.setBottom((targetRect.top() / h) * 2.0f - 1.0f);
		if (targetRect.bottom() == (targetRect.height() - 1))
		{
			r.setTop(1.0f);
		}
		else
		{
			r.setTop((targetRect.bottom() / w) * 2.0f - 1.0f);
		}
		drawTexture(r, sourceRect);
		blitProgram->release();
	#else
		glBindTexture(texture_type_, texture_id_);
		// (In OpenGL coordinates Y is reversed)
		const uint bottom = targetRect.height() - (sourceRect.y() + sourceRect.height());
		glCopyTexSubImage2D(target, 0, sourceRect.x(), bottom, sourceRect.x(), bottom, sourceRect.width(), sourceRect.height());
		glBindTexture(texture_type_, 0);
		glDisable(GL_DEPTH_TEST);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		#ifndef QT_OPENGL_ES
			glOrtho(0, targetRect.width(), targetRect.height(), 0, -999999, 999999);
		#else
			glOrthof(0, targetRect.width(), targetRect.height(), 0, -999999, 999999);
		#endif
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		drawTexture(targetRect, targetRect.size(), sourceRect);
	#endif
}

void QOpenGLTextureHolder::drawTexture(const QRectF & rect, const QRectF & bitmap_rect)
{
	if (!isAllocated())
	{
		qWarning()<<"Attempt to draw an unallocated texture!";
		return;
	}

	// src - source rectangle (in the texture)
	QRectF src = bitmap_rect.isEmpty()
		// Source region not specified, will use whole texture
		? QRectF(QPointF(), texture_size_)
		// Using a piece of texture
		: QRectF(
			 // (Reverse Y axis)
			 QPointF(bitmap_rect.x(), texture_size_.height() - bitmap_rect.bottom())
			 , bitmap_rect.size());

	// Convert in-texture coords to [0..1]
	qreal width = texture_size_.width();
	qreal height = texture_size_.height();

	GLfloat tx1 = src.left() / width;
	GLfloat tx2 = src.right() / width;
	GLfloat ty1 = src.top() / height;
	GLfloat ty2 = src.bottom() / height;

	GLfloat texCoordArray[4*2];

	// Apply texture transformation. This basically reverses Y axis on all
	// tested Android devices, but who knows if it is always that simple or not?
	GLfloat tx1m = a11_ * tx1 + a12_ * ty1 + b1_;
	GLfloat tx2m = a11_ * tx2 + a12_ * ty2 + b1_;
	GLfloat ty1m = a21_ * tx1 + a22_ * ty1 + b2_;
	GLfloat ty2m = a21_ * tx2 + a22_ * ty2 + b2_;

	// Put the in-texture coordinates into the array for GL.
	texCoordArray[0] = tx1m;
	texCoordArray[1] = ty2m;
	texCoordArray[2] = tx2m;
	texCoordArray[3] = ty2m;
	texCoordArray[4] = tx2m;
	texCoordArray[5] = ty1m;
	texCoordArray[6] = tx1m;
	texCoordArray[7] = ty1m;

	// Put the rectangle coordinates into the vertex array for GL.
	GLfloat vertexArray[4*2];
	QRectFToVertexArray(rect, vertexArray);

	#if !defined(QT_OPENGL_ES_2)
		glVertexPointer(2, GL_FLOAT, 0, vertexArray);
		glTexCoordPointer(2, GL_FLOAT, 0, texCoordArray);

		glBindTexture(texture_type_, texture_id_);
		glEnable(texture_type_);

		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);

		glDisable(texture_type_);
		glBindTexture(texture_type_, 0);
	#else
		glVertexAttribPointer(c_vertex_coordinates_attr, 2, GL_FLOAT, GL_FALSE, 0, vertexArray);
		glVertexAttribPointer(c_texture_coordinates_attr, 2, GL_FLOAT, GL_FALSE, 0, texCoordArray);

		glBindTexture(texture_type_, texture_id_);

		glEnableVertexAttribArray(c_vertex_coordinates_attr);
		glEnableVertexAttribArray(c_texture_coordinates_attr);

		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

		glDisableVertexAttribArray(c_vertex_coordinates_attr);
		glDisableVertexAttribArray(c_texture_coordinates_attr);
		glBindTexture(texture_type_, 0);
	#endif
}

void QOpenGLTextureHolder::allocateTexture()
{
	deallocateTexture();

	glGenTextures(1, &texture_id_);

	glBindTexture(texture_type_, texture_id_);
	glTexParameteri(texture_type_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(texture_type_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(texture_type_, 0);
}

void QOpenGLTextureHolder::allocateTexture(GLenum type)
{
	Q_ASSERT(type == GL_TEXTURE_EXTERNAL_OES || type == GL_TEXTURE_2D);
	texture_type_ = type;
	allocateTexture();
}

void QOpenGLTextureHolder::initializeGL()
{
	GetBlitProgram(GL_TEXTURE_2D);
	GetBlitProgram(GL_TEXTURE_EXTERNAL_OES);
}

