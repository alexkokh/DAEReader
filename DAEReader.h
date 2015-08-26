#pragma once

#include <inttypes.h>
#include <vector>

using namespace std;

namespace DAE_READER
{
	typedef struct elements_s
	{
		int vertexOffset;
		int normalOffset;
		int texcoordOffset;
		int elementsCount;
	} elements_t;

	typedef struct draw_range_s
	{
		int offset;
		int count;
	} draw_range_t;

	typedef struct geometry_s
	{
		string name;
		uint32_t maxIndex;
		vector<int> vertexOffset;
		vector<int> normalOffset;
		vector<int> texcoordOffset;
		vector<float> vertices;
		vector<float> normals;
		vector<float> texcoords;
		vector<vector<int>> triangles;
		vector<uint32_t> indices;
		vector<float> bufferData;
		vector<vector<draw_range_t>> meshes;
	} geometry_t;

	typedef struct dae_reader_s
	{
		int x;
		int y;
		int z;
		vector<geometry_t> geometry;
	} dae_reader_t;

	dae_reader_t *createDAEReader(char *path);
	void destroyDAEReader(dae_reader_t *reader);
	int getElementSize(dae_reader_t *reader);
}