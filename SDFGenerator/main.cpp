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
	return (fabs(lhs.redF() - rhs.redF()) < 0.005) &&
	(fabs(lhs.greenF() - rhs.greenF()) < 0.005) &&
	(fabs(lhs.blueF() - rhs.blueF()) < 0.005) &&
	(fabs(lhs.alphaF() - rhs.alphaF()) < 0.005);
}

auto bitmap(int argc, const char* argv[])
{
	QImage img(argv[1], "PNG");
	auto color1 = QColor(argv[2]);
	auto color2 = QColor(argv[3]);
	auto color3 = QColor(argv[4]);
	QImage out(img.width(), img.height(), QImage::Format_Grayscale16);
	qWarning() << "preparing bitmap" << color1 << color2 << color3;
	QSet<QColor> colors;
	for (int y = 0; y < img.height(); y++) {
		auto line = reinterpret_cast<uint16_t*>(out.scanLine(y));

		for (int x = 0; x < img.width(); x++) {
			auto color = img.pixelColor(x, y);
			colors.insert(color);
			auto it = compare(color, color1) || compare(color, color2) || compare(color, color3);
			line[x] = it ? std::numeric_limits<quint16>::max() : 0;
		}
	}
	qWarning() << QList<QColor>(colors.cbegin(), colors.cend());
	out.save(argv[5], "PNG");
}

auto sdf(int argc, const char* argv[])
{
	QImage img(argv[1], "PNG");
	vector<bool> mask(img.width() * img.height());
	for (int y = 0; y < img.height(); y++) {
		auto line = reinterpret_cast<uint16_t*>(img.scanLine(y));
		auto base = mask.begin() + y*img.width();
		for (int x = 0; x < img.width(); x++) {
			base[x] = line[x] != 0;
		}
	}

	qWarning() << "starting sdf";
	auto result = sdf(mask, img.width());

	QFile file(argv[2]);
	file.open(QIODevice::WriteOnly);
	QDataStream dso(&file);
	dso << QVector<float>(result.cbegin(), result.cend());
}

auto output(int argc, const char* argv[])
{
	QImage img(argv[1], "PNG");

	QFile file(argv[2]);
	file.open(QIODevice::ReadOnly);
	QDataStream dso(&file);
	QVector<float> it;
	dso >> it;

	QImage out(img.width(), img.height(), QImage::Format_RGBA64);
	qWarning() << "making grayscale output" << out.width() << out.height() << out.sizeInBytes();
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
	out.save(argv[3], "PNG");
}

int main(int argc, const char* argv[])
{
	output(argc, argv);
}
