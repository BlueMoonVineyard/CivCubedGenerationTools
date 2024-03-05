#include <QString>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLBuffer>
#include <QImage>
#include <QDebug>
#include <QGuiApplication>
#include <QImageReader>
#include <QFile>

QString readFile(const char *file)
{
	QFile f(file);
	if (!f.open(QFile::ReadOnly | QFile::Text)) return "";
	QTextStream in(&f);
	return in.readAll();
}

void processImage(
	const QImage& land,
	const QImage& mountain,
	QImage& out
)
{
	QOpenGLContext context;
	if (!context.create()) {
		qWarning() << "Failed to create QOpenGLContext";
		return;
	}

	QOffscreenSurface surface;
	surface.setFormat(context.format());
	surface.create();
	if(!surface.isValid())
	{
		qWarning() << "Failed to create QOffscreenSurface";
		return;
	}

	if(!context.makeCurrent(&surface))
	{
		qWarning() << "Failed to make QOpenGLContext current.";
		return;
	}

	QOpenGLFramebufferObject fbo(land.size());
	context.functions()->glViewport(0, 0, land.width(), land.height());

	QOpenGLShaderProgram program(&context);
	if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex, readFile("BiomeDistributor/shader.vert")))
	{
		qWarning() << "Can't add vertex shader.";
		return;
	}
	if (!program.addShaderFromSourceCode(QOpenGLShader::Fragment, readFile("BiomeDistributor/shader.frag")))
	{
		qWarning() << "Can't add fragment shader.";
		return;
	}
	if (!program.link())
	{
		qWarning() << "Can't link program.";
		return;
	}
	if (!program.bind())
	{
		qWarning() << "Can't bind program.";
		return;
	}

	QOpenGLTexture landTexture(QOpenGLTexture::Target2D);
	landTexture.setData(land);

	landTexture.bind(0);
	if(!landTexture.isBound())
	{
		qWarning() << "Land texture not bound.";
		return;
	}

	QOpenGLTexture mountainTexture(QOpenGLTexture::Target2D);
	mountainTexture.setData(mountain);

	mountainTexture.bind(1);
	if(!mountainTexture.isBound())
	{
		qWarning() << "Mountain texture not bound.";
		return;
	}

	struct VertexData
	{
		QVector2D position;
		QVector2D texCoord;
	};

	VertexData vertices[] =
	{
		{{ -1.0f, +1.0f }, { 0.0f, 1.0f }}, // top-left
		{{ +1.0f, +1.0f }, { 1.0f, 1.0f }}, // top-right
		{{ -1.0f, -1.0f }, { 0.0f, 0.0f }}, // bottom-left
		{{ +1.0f, -1.0f }, { 1.0f, 0.0f }}  // bottom-right
	};

	GLuint indices[] =
	{
		0, 1, 2, 3
	};

	QOpenGLBuffer vertexBuf(QOpenGLBuffer::VertexBuffer);
	QOpenGLBuffer indexBuf(QOpenGLBuffer::IndexBuffer);

	if(!vertexBuf.create())
	{
		qWarning() << "Can't create vertex buffer.";
		return;
	}

	if(!indexBuf.create())
	{
		qWarning() << "Can't create index buffer.";
		return;
	}

	if(!vertexBuf.bind())
	{
		qWarning() << "Can't bind vertex buffer.";
		return;
	}
	vertexBuf.allocate(vertices, 4 * sizeof(VertexData));

	if(!indexBuf.bind())
	{
		qWarning() << "Can't bind index buffer.";
		return;
	}
	indexBuf.allocate(indices, 4 * sizeof(GLuint));

	int offset = 0;
	program.enableAttributeArray("aPosition");
	program.setAttributeBuffer("aPosition", GL_FLOAT, offset, 2, sizeof(VertexData));
	offset += sizeof(QVector2D);
	program.enableAttributeArray("aTexCoord");
	program.setAttributeBuffer("aTexCoord", GL_FLOAT, offset, 2, sizeof(VertexData));
	program.setUniformValue("landSampler", 0);
	program.setUniformValue("mountainSampler", 1);

	qWarning() << "Rendering biomes...";
	context.functions()->glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_INT, Q_NULLPTR);

	out = fbo.toImage(false);
}

int main(int argc, char* argv[])
{
	QGuiApplication app(argc, argv);
	QImageReader::setAllocationLimit(0);
	qWarning() << "Loading SDFs...";
	QImage landSDF("Data/area.png", "PNG");
	QImage mountainSDF("Data/mountain.png", "PNG");
	QImage out;
	processImage(landSDF, mountainSDF, out);
	out.save("Out.png", "PNG");
	return 0;
}
