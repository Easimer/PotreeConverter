
#pragma once

#include <execution>
#include <numeric>

#include "structures.h"
#include "Attributes.h"
#include "PotreeConverter.h"


struct SamplerPoisson : public Sampler {
	struct PointIndex {
		size_t pointIndex;
		uint8_t childIndex;
	};

	struct f32x3 {
		float x, y, z;

		f32x3() : x(0.0f), y(0.0f), z(0.0f) {}
		f32x3(float x, float y, float z) : x(x), y(y), z(z) {}
		f32x3(const Vector3& in) : x(in.x), y(in.y), z(in.z) {}

		f32x3 operator*(const f32x3& rhs) const {
			return f32x3{ x * rhs.x, y * rhs.y, z * rhs.z };
		}

		f32x3 operator+(const f32x3& rhs) const {
			return f32x3{ x + rhs.x, y + rhs.y, z + rhs.z };
		}

		f32x3 operator-(const f32x3& rhs) const {
			return f32x3{ x - rhs.x, y - rhs.y, z - rhs.z };
		}

		float dot(const f32x3& rhs) const {
			return x * rhs.x + y * rhs.y + z * rhs.z;
		}

		float lengthSq() const {
			return x * x + y * y + z * z;
		}

		float length() const {
			return sqrtf(lengthSq());
		}
	};

	// subsample a local octree from bottom up
	void sample(Node* node, Attributes &attributes, double baseSpacing, 
		function<void(Node*)> onNodeCompleted, 
		function<void(Node*)> onNodeDiscarded
	) {


		function<void(Node*, function<void(Node*)>)> traversePost = [&traversePost](Node* node, function<void(Node*)> callback) {
			for (auto child : node->children) {

				if (child != nullptr && !child->sampled) {
					traversePost(child.get(), callback);
				}
			}

			callback(node);
		};

		vector<f32x3> pointPos;
		vector<PointIndex> pointIdx;
		vector<size_t> pointIndices;

		vector<f32x3> rel;
		vector<float> distSq;

		vector<size_t> dbgAccepted(1'000'000);

		traversePost(node, [baseSpacing, &onNodeCompleted, &onNodeDiscarded, &attributes, &pointPos, &pointIdx, &pointIndices, &rel, &distSq, &dbgAccepted](Node* node) {
			node->sampled = true;

			int64_t numPoints = node->numPoints;

			auto max = node->max;
			auto min = node->min;
			auto size = max - min;
			auto scale = attributes.posScale;
			auto offset = attributes.posOffset;

			const size_t sizPoint = attributes.bytes;

			bool isLeaf = node->isLeaf();

			if (isLeaf) {
				return false;
			}

			// =================================================================
			// SAMPLING
			// =================================================================
			//
			// first, check for each point whether it's accepted or rejected
			// save result in an array with one element for each point

			size_t numPointsInChildren = 0;
			for (auto child : node->children) {
				if (child == nullptr) {
					continue;
				}

				numPointsInChildren += child->numPoints;
			}

			pointPos.clear();
			pointPos.reserve(numPointsInChildren);
			pointIdx.clear();
			pointIdx.reserve(numPointsInChildren);

			pointIndices.resize(numPointsInChildren);
			iota(pointIndices.begin(), pointIndices.end(), 0);

			vector<vector<int8_t>> acceptedChildPointFlags;
			vector<int64_t> numRejectedPerChild(8, 0);
			int64_t numAccepted = 0;

			for (uint8_t childIndex = 0; childIndex < 8; childIndex++) {
				Node *child = node->children[childIndex].get();

				if (child == nullptr) {
					acceptedChildPointFlags.push_back({});
					//numRejectedPerChild.push_back({});

					continue;
				}

				acceptedChildPointFlags.push_back(vector<int8_t>(child->numPoints, 0));

				for (size_t i = 0; i < child->numPoints; i++) {
					size_t pointOffset = i * sizPoint;
					auto* xyz = reinterpret_cast<const int32_t*>(child->points->data_u8 + pointOffset);

					double x = (xyz[0] * scale.x) + offset.x;
					double y = (xyz[1] * scale.y) + offset.y;
					double z = (xyz[2] * scale.z) + offset.z;

					pointPos.push_back(f32x3(x, y, z));
					pointIdx.push_back({ i, childIndex });
				}

			}

			// unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();

			size_t dbgNumAccepted = 0;
			double spacing = baseSpacing / pow(2.0, node->level());
			double squaredSpacing = spacing * spacing;

			f32x3 center = (node->min + node->max) * 0.5;

			// Precompute relative position to the node center and the squared
			// length of this vector
			rel.resize(numPointsInChildren);
			distSq.resize(numPointsInChildren);

			for (size_t i = 0; i < numPointsInChildren; i++) {
				f32x3 c = pointPos[i] - center;
				rel[i] = c;
				distSq[i] = c.lengthSq();
			}

			//int dbgChecks = -1;
			//int dbgSumChecks = 0;
			//int dbgMaxChecks = 0;

			auto checkAccept = [/*&dbgChecks, &dbgSumChecks,*/ &dbgAccepted, &dbgNumAccepted, spacing, squaredSpacing, &pointPos, &rel, &distSq /*, &numDistanceChecks*/](size_t idxCandidate) {
				f32x3 candidate = pointPos[idxCandidate];
				float cd = sqrtf(distSq[idxCandidate]);
				auto limit = (cd - spacing);
				float limitSquared = limit * limit;
				float ss = squaredSpacing;

				uint16_t j = 0;
				for (size_t i = dbgNumAccepted - 1; i < dbgNumAccepted; i--) {
					size_t idxPoint = dbgAccepted[i];

					//dbgChecks++;
					//dbgSumChecks++;

					// check distance to center
					float pdd = distSq[idxPoint];

					// stop when differences to center between candidate and accepted exceeds the spacing
					// any other previously accepted point will be even closer to the center.
					if (pdd < limitSquared) {
						return true;
					}

					float dd = (pointPos[idxPoint] - candidate).lengthSq();

					if (dd < ss) {
						return false;
					}

					j++;

					// also put a limit at x distance checks
					if (j > 10'000) {
						return true;
					}
				}

				return true;

			};

			auto parallel = std::execution::par_unseq;
			std::sort(parallel, pointIndices.begin(), pointIndices.end(), [&distSq](size_t lhs, size_t rhs) -> bool {
				// sort by distance to center
				float distLhs = distSq[lhs];
				float distRhs = distSq[rhs];
				return distLhs < distRhs;
			});

			for (size_t idxPoint : pointIndices) {
				PointIndex idx = pointIdx[idxPoint];

				//dbgChecks = 0;

				bool isAccepted = checkAccept(idxPoint);

				//dbgMaxChecks = std::max(dbgChecks, dbgMaxChecks);

				if (isAccepted) {
					dbgAccepted[dbgNumAccepted] = idxPoint;
					dbgNumAccepted++;
					numAccepted++;
				} else {
					numRejectedPerChild[idx.childIndex]++;
				}

				//{ // debug: store sample time in GPS time attribute
				//		auto child = node->children[point.childIndex];
				//		auto data = child->points->data_u8;

				//		//static double value = 0.0;


				//		//value += 0.1;

				//		//if(node->level() <= 2){
				//			std::lock_guard<mutex> lock(mtx_debug);

				//			debug += 0.01;
				//			memcpy(data + point.pointIndex * attributes.bytes + 20, &debug, 8);
				//		//}
				//		//double value = now();

				//		//point.pointIndex
				//}

				acceptedChildPointFlags[idx.childIndex][idx.pointIndex] = isAccepted ? 1 : 0;

				//abc++;

			}

			auto accepted = make_shared<Buffer>(numAccepted * sizPoint);
			for (uint8_t childIndex = 0; childIndex < 8; childIndex++) {
				auto child = node->children[childIndex];

				if (child == nullptr) {
					continue;
				}

				auto numRejected = numRejectedPerChild[childIndex];
				auto& acceptedFlags = acceptedChildPointFlags[childIndex];
				auto rejected = make_shared<Buffer>(numRejected * sizPoint);

				for (size_t i = 0; i < child->numPoints; i++) {
					bool isAccepted = acceptedFlags[i];
					size_t pointOffset = i * sizPoint;

					if (isAccepted) {
						accepted->write(child->points->data_u8 + pointOffset, sizPoint);
						// rejected->write(child->points->data_u8 + pointOffset, attributes.bytes);
					} else {
						rejected->write(child->points->data_u8 + pointOffset, sizPoint);
					}
				}

				if (numRejected == 0 && child->isLeaf()) {
					onNodeDiscarded(child.get());

					node->children[childIndex] = nullptr;
				} if (numRejected > 0) {
					child->points = rejected;
					child->numPoints = numRejected;

					onNodeCompleted(child.get());
				} else if(numRejected == 0) {
					// the parent has taken all points from this child, 
					// so make this child an empty inner node.
					// Otherwise, the hierarchy file will claim that 
					// this node has points but because it doesn't have any,
					// decompressing the nonexistent point buffer fails
					// https://github.com/potree/potree/issues/1125
					child->points = nullptr;
					child->numPoints = 0;
					onNodeCompleted(child.get());
				}
			}

			node->points = accepted;
			node->numPoints = numAccepted;

			//{ // debug
			//	auto avgChecks = dbgSumChecks / points.size();
			//	string msg = "#checks: " + formatNumber(dbgSumChecks) + ", maxChecks: " + formatNumber(dbgMaxChecks) + ", avgChecks: " + formatNumber(avgChecks) + "\n";
			//	cout << msg;
			//}

			return true;
		});
	}

};

