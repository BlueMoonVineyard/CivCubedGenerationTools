#include <cassert>
#include <cmath>
#include <functional>
#include <limits>
#include <qrgba64.h>
#include <queue>
#include <utility>
#include <vector>
#include <QImage>
#include <QDebug>
#include <QFile>
#include <QDataStream>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QImageReader>

using namespace std;

// We use 64-bit integers to avoid some annoying integer
// math
// overflow corner cases.
using Metric = function<float(int64_t, int64_t)>;
float euclidian(int64_t dx, int64_t dy) { return sqrt(dx * dx + dy * dy); }

void sdf_partial(const vector<bool> &in_filled, int width,
								 vector<pair<int, int>> *in_half_vector, Metric metric,
								 bool negate) {
	assert(width != 0);
	const long height = in_filled.size() / width;
	assert(height != 0);
	auto valid_pixel = [&](int x, int y) {
		return (x >= 0) && (x < width) && (y >= 0) && (y < height);
	};
	auto coord = [&](int x, int y) { return x + width * y; };
	auto filled = [&](int x, int y) -> bool {
		if (valid_pixel(x, y))
			return in_filled[coord(x, y)] ^ negate;
		return false ^ negate;
	};
	// Allows us to write loops over a neighborhood of a cell.
	auto do_neighbors = [&](int x, int y, function<void(int, int)> f) {
		for (int dy = -1; dy <= 1; dy++)
			for (int dx = -1; dx <= 1; dx++)
				if (valid_pixel(x + dx, y + dy))
					f(x + dx, y + dy);
	};
	auto &half_vector = *in_half_vector;
	vector<bool> closed(in_filled.size());
	struct QueueElement {
		int x, y, dx, dy;
		float dist;
	};
	struct QueueCompare {
		bool operator()(QueueElement &a, QueueElement &b) {
			return a.dist > b.dist;
		}
	};
	priority_queue<QueueElement, vector<QueueElement>, QueueCompare> pq;
	auto add_to_queue = [&](int x, int y, int dx, int dy) {
		pq.push({x, y, dx, dy, metric(dx, dy)});
	};
	qWarning() << "  seed phase";
	// A. Seed phase: Find all filled (black) pixels that border an
	// empty pixel. Add half distances to every surrounding unfilled
	// (white) pixel.
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			if (filled(x, y)) {
				do_neighbors(x, y, [&](int x2, int y2) {
					if (!filled(x2, y2))
						add_to_queue(x2, y2, x2 - x, y2 - y);
				});
			}
		}
	}
	qWarning() << "  propagation phase";
	// B. Propagation phase: Add surrounding pixels to queue and
	// discard the ones that are already closed.
	while (!pq.empty()) {
		auto current = pq.top();
		pq.pop();
		// If it's already been closed then the shortest vector has
		// already been found.
		if (closed[coord(current.x, current.y)])
			continue;
		// Close this one and store the half vector.
		closed[coord(current.x, current.y)] = true;
		half_vector[coord(current.x, current.y)] = {current.dx, current.dy};
		// Add all open neighbors to the queue.
		do_neighbors(current.x, current.y, [&](int x2, int y2) {
			if (!filled(x2, y2) && !closed[coord(x2, y2)]) {
				int dx = 2 * (x2 - current.x);
				int dy = 2 * (y2 - current.y);
				auto [ddx, ddy] = half_vector[coord(current.x, current.y)];
				dx += ddx;
				dy += ddy;
				add_to_queue(x2, y2, dx, dy);
			}
		});
	}
}

vector<float> sdf(const vector<bool> &in_filled, int width) {
	const auto height = in_filled.size() / width;
	// Initialize vectors represented as half values.
	vector<pair<int, int>> half_vector(in_filled.size(),
																		 {2 * width + 1, 2 * height + 1});
	qWarning() << " sdf false";
	sdf_partial(in_filled, width, &half_vector, euclidian, false);
	qWarning() << " sdf true";
	sdf_partial(in_filled, width, &half_vector, euclidian, true);
	vector<float> out(in_filled.size());
	for (size_t i = 0; i < half_vector.size(); i++) {
		auto [dx, dy] = half_vector[i];
		out[i] = euclidian(dx, dy) / 2;
		if (in_filled[i])
			out[i] = -out[i];
	}
	return out;
}

QT_BEGIN_NAMESPACE
uint qHash(const QColor &c)
{
		return qHash(c.rgba());
}
QT_END_NAMESPACE

inline bool compare(const QColor& lhs, const QColor &rhs)
{
	if (!lhs.isValid() || !rhs.isValid()) return false;
	return (fabs(lhs.redF() - rhs.redF()) < 0.005) &&
	(fabs(lhs.greenF() - rhs.greenF()) < 0.005) &&
	(fabs(lhs.blueF() - rhs.blueF()) < 0.005) &&
	(fabs(lhs.alphaF() - rhs.alphaF()) < 0.005);
}

auto bitmap(const QString& input, const QString& output, const QList<QColor>& colors)
{
	QImage img(input, "PNG");
	auto color1 = colors.value(0);
	auto color2 = colors.value(1);
	auto color3 = colors.value(2);
	QImage out(img.width(), img.height(), QImage::Format_Grayscale16);
	qWarning() << "Preparing bitmap..." << colors.mid(0, 3);
	for (int y = 0; y < img.height(); y++) {
		auto line = reinterpret_cast<uint16_t*>(out.scanLine(y));

		for (int x = 0; x < img.width(); x++) {
			auto color = img.pixelColor(x, y);
			auto it = compare(color, color1) || compare(color, color2) || compare(color, color3);
			line[x] = it ? std::numeric_limits<quint16>::max() : 0;
		}
	}
	out.save(output, "PNG");
}

auto sdf(const QString &input, const QString& output)
{
	QImage img(input, "PNG");
	vector<bool> mask(img.width() * img.height());
	for (int y = 0; y < img.height(); y++) {
		auto line = reinterpret_cast<uint16_t*>(img.scanLine(y));
		auto base = mask.begin() + y*img.width();
		for (int x = 0; x < img.width(); x++) {
			base[x] = line[x] != 0;
		}
	}

	qWarning() << "Generating SDF...";
	auto result = sdf(mask, img.width());

	QFile file(output);
	file.open(QIODevice::WriteOnly);
	QDataStream dso(&file);
	dso << quint32(img.width()) << quint32(img.height());
	dso << QVector<float>(result.cbegin(), result.cend());
}

auto output(const QString &input, const QString &output)
{
	QFile file(input);
	file.open(QIODevice::ReadOnly);
	QDataStream dso(&file);
	quint32 width, height;
	QVector<float> it;
	dso >> width;
	dso >> height;
	dso >> it;

	QImage out(width, height, QImage::Format_RGBA64);
	qWarning() << "Generating grayscale image..." << out.width() << out.height();
	const double min = *std::min_element(it.cbegin(), it.cend());
	const double max = *std::max_element(it.cbegin(), it.cend());
	const double dx = max-min;

	qWarning() << min << max;
	const double min16 = std::numeric_limits<quint16>::min();
	const double max16 = std::numeric_limits<quint16>::max();
	qWarning() << (0-min)/dx;
	for (int y = 0; y < out.height(); y++) {
		auto line = reinterpret_cast<QRgba64*>(out.scanLine(y));
		auto base = it.begin()+y*out.width();
		for (int x = 0; x < out.width(); x++) {
			const auto v = base[x];
			const auto mv = ((v-min)/dx)*max16;

			if (v < 0) {
				line[x] = qRgba64(0, 												 0, (abs(v)/abs(min)) * max16, max16);
			} else {
				line[x] = qRgba64(0, (abs(v)/abs(max)) * max16, 												0, max16);
			}
		}
	}
	out.save(output, "PNG");
}

int main(int argc, char* argv[])
{
	QCoreApplication app(argc, argv);
	QImageReader::setAllocationLimit(0);
	QCoreApplication::setApplicationName("SDFGenerator");
	QCoreApplication::setApplicationVersion("1.0");
	QCommandLineParser parser;

	parser.setApplicationDescription("SDF utilities for CivCubed");
	parser.addHelpOption();
	parser.addVersionOption();
	parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsOptions);
	parser.addPositionalArgument("command", "prepare-bitmap | generate-sdf | sdf-to-png");

	parser.process(app);

	if (parser.positionalArguments().length() < 1) {
		qWarning() << "Missing command.";
		parser.showHelp();
		return 1;
	}

	const auto command = parser.positionalArguments().at(0);
	if (command == "prepare-bitmap") {
		parser.addPositionalArgument("input", "input image file");
		parser.addPositionalArgument("output", "output image file");
		parser.addPositionalArgument("colors...", "colors to count as interior");
		parser.process(app);
		if (parser.positionalArguments().length() < 4) {
			qWarning() << "Missing parameters.";
			parser.showHelp();
			return 1;
		}
		const auto in = parser.positionalArguments().at(1);
		const auto out = parser.positionalArguments().at(2);
		const auto colors = parser.positionalArguments().mid(3);
		QList<QColor> kolors;
		for (const auto& color : colors) {
			QColor kolor(color);
			if (!kolor.isValid()) {
				qWarning() << "Invalid color" << kolor;
				return 1;
			}
			kolors.append(kolor);
		}
		bitmap(in, out, kolors);
	} else if (command == "generate-sdf") {
		parser.addPositionalArgument("input", "input image file");
		parser.addPositionalArgument("output", "output sdf file");
		parser.process(app);
		if (parser.positionalArguments().length() < 3) {
			qWarning() << "Missing parameters.";
			parser.showHelp();
			return 1;
		}
		const auto in = parser.positionalArguments().at(1);
		const auto out = parser.positionalArguments().at(2);
		sdf(in, out);
	} else if (command == "sdf-to-png") {
		parser.addPositionalArgument("input", "input sdf file");
		parser.addPositionalArgument("output", "output image file");
		parser.process(app);
		if (parser.positionalArguments().length() < 3) {
			qWarning() << "Missing parameters.";
			parser.showHelp();
			return 1;
		}
		const auto in = parser.positionalArguments().at(1);
		const auto out = parser.positionalArguments().at(2);
		output(in, out);
	} else {
		qWarning() << "Invalid command.";
		parser.showHelp();
		return 1;
	}
	// qWarning() << parser.positionalArguments();
	// output(argc, argv);
}
