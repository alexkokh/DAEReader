#include <Windows.h>
#include <string>
#include <algorithm>
#include <sstream>
#include <time.h>
#include "DAEReader.h" 

#import <msxml6.dll> 

namespace DAE_READER
{
	enum ElementOffsets { VertexOffset = 0, NormalOffset = 3, TexcoordOffset = 6 };

	template<typename T>
	T parseValue(string);

	template<>
	float parseValue<float>(string value)
	{
		return roundf((float)atof(value.c_str()) * 100000.f) / 100000.f; // due to the blender's rounding issues
	}

	template<>
	int parseValue<int>(string value)
	{
		return atoi(value.c_str());
	}

	template<typename T> 
	vector<T> readValues(MSXML2::IXMLDOMNodePtr array)
	{
		vector<T> v;
		string vertices = array->text;
		replace(vertices.begin(), vertices.end(), '\n', ' '); // 3dsmax
		stringstream ss(vertices);
		string item;

		while (getline(ss, item, ' '))
		{
			v.push_back(parseValue<T>(item));
		}

		return v;
	}

	template<typename T> vector<T> readArray(MSXML2::IXMLDOMNodePtr source)
	{
		for (int i = 0; i < source->childNodes->length; i++)
		{
			MSXML2::IXMLDOMNodePtr node = source->childNodes->item[i];

			_bstr_t name = node->baseName;
			if (wcsstr((wchar_t *)node->baseName, L"array"))
			{
				return readValues<T>(node);
			}
		}

		return vector<T>();
	}

	void readMesh(dae_reader_t *reader, MSXML2::IXMLDOMNodePtr mesh);
	void getAxisOrientation(dae_reader_t *reader, MSXML2::IXMLDOMDocument2Ptr xmlDoc);
	void readGeometry(dae_reader_t *reader, MSXML2::IXMLDOMDocument2Ptr xmlDoc);
	MSXML2::IXMLDOMDocument2Ptr setupXMLParser(char *path);
	dae_reader_t* init();
	void buildIndices(dae_reader_t *reader, int geometry_id);

	dae_reader_t *createDAEReader(char *path)
	{
		dae_reader_t *reader = init();
		MSXML2::IXMLDOMDocument2Ptr xmlDoc = setupXMLParser(path);
		getAxisOrientation(reader, xmlDoc);
		readGeometry(reader, xmlDoc);

		long l0 = clock();

		for (uint32_t i = 0; i < reader->geometry.size(); i++)
		{
			buildIndices(reader, i);
		}

		long l1 = clock();
		printf("%i\n", l1 - l0);

		return reader;
	}

	dae_reader_t* init()
	{
		dae_reader_t *reader = (dae_reader_t *)malloc(sizeof(dae_reader_s));
		memset(reader, 0, sizeof(dae_reader_s));
		return reader;
	}

	MSXML2::IXMLDOMDocument2Ptr setupXMLParser(char *path)
	{
		HRESULT hr;
		MSXML2::IXMLDOMDocument2Ptr xmlDoc;
		
		hr = CoInitialize(NULL);
		hr = xmlDoc.CreateInstance(__uuidof(MSXML2::DOMDocument60), NULL, CLSCTX_INPROC_SERVER);
		if (FAILED(hr)) return NULL;

		if (xmlDoc->load(path) != VARIANT_TRUE) return NULL;

		hr = xmlDoc->setProperty("SelectionNamespaces", "xmlns:r='http://www.collada.org/2005/11/COLLADASchema'");
		hr = xmlDoc->setProperty("SelectionLanguage", "XPath");

		return xmlDoc;
	}

	void getAxisOrientation(dae_reader_t *reader, MSXML2::IXMLDOMDocument2Ptr xmlDoc)
	{
		MSXML2::IXMLDOMNodePtr node = xmlDoc->selectSingleNode("/r:COLLADA/r:asset/r:up_axis");

		reader->x = 0;
		reader->y = 1;
		reader->z = 2;

		if (node != NULL)
		{
			string orientation = node->text;

			if (orientation[0] == 'Y')
			{
				reader->z = 1;
				reader->y = 2;
			}
		}
	}

	void readGeometry(dae_reader_t *reader, MSXML2::IXMLDOMDocument2Ptr xmlDoc)
	{
		MSXML2::IXMLDOMNodeListPtr nodeList;
		nodeList = xmlDoc->selectNodes("/r:COLLADA/r:library_geometries/r:geometry");
		int count = nodeList->length;

		for (int i = 0; i < count; i++)
		{
			MSXML2::IXMLDOMNodePtr node = nodeList->Getitem(i);
			readMesh(reader, node);
		}
	}

	void destroyDAEReader(dae_reader_t *reader)
	{
		reader->geometry.clear();
		free(reader);
		CoUninitialize();
	}

	void readVerticesNormalsTexcoords(MSXML2::IXMLDOMNodePtr mesh, geometry_t &geometry)
	{
		MSXML2::IXMLDOMNodeListPtr sourceList = mesh->selectNodes("r:source");
		vector<float> vertices;
		vector<float> normals;
		vector<float> texcoords;

		for (int i = 0; i < sourceList->length; i++)
		{
			MSXML2::IXMLDOMNodePtr source = sourceList->Getitem(i);
			MSXML2::IXMLDOMNamedNodeMapPtr attributes = source->attributes;

			for (int j = 0; j < attributes->length; j++)
			{
				_bstr_t attributeName = attributes->getNamedItem("id")->text;

				if (wcsstr(_wcslwr(attributeName), L"position"))
				{
					geometry.vertices = readArray<float>(source);
				} else if (wcsstr(_wcslwr(attributeName), L"normal"))
				{
					geometry.normals = readArray<float>(source);
				} else if (wcsstr(_wcslwr(attributeName), L"map") ||	// belnder
						   wcsstr(_wcslwr(attributeName), L"-uv"))		// 3dsmax
				{
					geometry.texcoords = readArray<float>(source);
				}
			}
		}
	}

	void readTriangles(MSXML2::IXMLDOMNodePtr mesh, geometry_t &geometry)
	{
		MSXML2::IXMLDOMNodeListPtr polyLists = mesh->selectNodes("r:polylist"); // blender
		MSXML2::IXMLDOMNodePtr p;

		if (polyLists->length == 0)
			polyLists = mesh->selectNodes("r:triangles"); // 3dsmax

		for (int i = 0; i < polyLists->length; i++)
		{
			MSXML2::IXMLDOMNodePtr polylist = polyLists->item[i];
			MSXML2::IXMLDOMNodePtr p = polylist->selectSingleNode("r:p");
			if (p == NULL) continue;

			geometry.vertexOffset.push_back(-1);
			geometry.normalOffset.push_back(-1);
			geometry.texcoordOffset.push_back(-1);

			MSXML2::IXMLDOMNodeListPtr inputList = polylist->selectNodes("r:input");
			for (int j = 0; j < inputList->length; j++)
			{
				MSXML2::IXMLDOMNodePtr input = inputList->Getitem(j);
				MSXML2::IXMLDOMNamedNodeMapPtr attributes = input->attributes;

				string semantic = (_bstr_t)_wcslwr((wchar_t *)attributes->getNamedItem("semantic")->text);
				int offset = _wtoi(attributes->getNamedItem("offset")->text);

				if (semantic == "vertex")
					geometry.vertexOffset[geometry.vertexOffset.size() - 1] = offset;
				else if (semantic == "normal")
					geometry.normalOffset[geometry.normalOffset.size() - 1] = offset;
				else if (semantic == "texcoord")
					geometry.texcoordOffset[geometry.texcoordOffset.size() - 1] = offset;
			}

			vector<int> v = readValues<int>(p);
			geometry.triangles.push_back(v);
		}
	}

	void readMesh(dae_reader_t *reader, MSXML2::IXMLDOMNodePtr geometry)
	{
		MSXML2::IXMLDOMNodePtr mesh = geometry->selectSingleNode("r:mesh");
		if (mesh == NULL) return;

		geometry_t object;
		MSXML2::IXMLDOMNamedNodeMapPtr attributes = geometry->attributes;
		if (attributes->length > 0)
			object.name = attributes->getNamedItem("name")->text;

		readVerticesNormalsTexcoords(mesh, object);
		readTriangles(mesh, object);

		object.maxIndex = 0;
		reader->geometry.push_back(object);
	}

	elements_t getElementOffsets(geometry_t *geometry)
	{
		elements_t elements;
		int v = geometry->vertexOffset[0];
		int n = geometry->normalOffset[0];
		int t = geometry->texcoordOffset[0];

		elements.vertexOffset = v;
		elements.normalOffset = n;
		elements.texcoordOffset = t;

		int elementSize = 1;
		elementSize += n != -1 ? 1 : 0;
		elementSize += t != -1 ? 1 : 0;

		elements.elementsCount = elementSize;

		return elements;
	}

	int getElementSize(dae_reader_t *reader)
	{
		int elementSize = 3; // vertices
		bool normalsPresent = false;
		bool texcoordsPresent = false;

		for (uint32_t i = 0; i < reader->geometry.size(); i++)
		{
			if (normalsPresent && texcoordsPresent) break;

			if (reader->geometry[i].normals.size() > 0 && !normalsPresent)
			{
				normalsPresent = true;
				elementSize += 3;
			}

			if (reader->geometry[i].texcoords.size() > 0 && !texcoordsPresent)
			{
				texcoordsPresent = true;
				elementSize += 2;
			}
		}

		return elementSize;
	}

	void fillTempBuffer(dae_reader_t *reader, geometry_t const *geometry, int meshIndex, vector<float>& rawData)
	{
		const vector<int> &triangles = geometry->triangles[meshIndex];
		int v = geometry->vertexOffset[meshIndex];
		int n = geometry->normalOffset[meshIndex];
		int t = geometry->texcoordOffset[meshIndex];
		int elementSize = getElementSize(reader);
		int x = reader->x;
		int y = reader->y;
		int z = reader->z;

		int elementCount = 1;
		elementCount += n != -1 ? 1 : 0;
		elementCount += t != -1 ? 1 : 0;

		for (uint32_t i = 0, j = 0; i < triangles.size(); i += elementCount, j++)
		{
			int vSrc = i + v;
			int trv = triangles[vSrc];
			trv *= 3;
			rawData.push_back(geometry->vertices[trv + x]);
			rawData.push_back(geometry->vertices[trv + y]);
			rawData.push_back(geometry->vertices[trv + z]);

			if (n > -1)
			{
				int nSrc = i + n;
				int trn = triangles[nSrc];
				trn *= 3;
				rawData.push_back(geometry->normals[trn + x]);
				rawData.push_back(geometry->normals[trn + y]);
				rawData.push_back(geometry->normals[trn + z]);
			}

			if (t > -1)
			{
				int tSrc = i + t;
				int trt = 2 * triangles[tSrc];
				rawData.push_back(geometry->texcoords[trt + 0]);
				rawData.push_back(geometry->texcoords[trt + 1]);
			}
		}
	}

	void addVertex(vector<float>& src, vector<float>& dst, int startIndex, int elementSize)
	{
		for (int i = startIndex; i < startIndex + elementSize; i++)
		{
			dst.push_back(src[i]);
		}
	}

	int getBaseOffset(dae_reader_t *reader, int geometryID)
	{
		int baseOffset = 0;
		for (int i = 0; i < geometryID; i++)
		{
			vector<vector<draw_range_t>>& meshes = reader->geometry[i].meshes;
			for (uint32_t j = 0; j < meshes.size(); j++)
			{
				vector<draw_range_t>& ranges = meshes[j];
				for (uint32_t k = 0; k < ranges.size(); k++)
				{
					baseOffset += ranges[k].count;
				}
			}
		}

		return baseOffset;
	}

	int getTrianglesSize(geometry_t *geometry)
	{
		int totalTrianglesSize = 0;

		for (uint32_t meshes = 0; meshes < geometry->triangles.size(); meshes++)
		{
			totalTrianglesSize += geometry->triangles[meshes].size();
		}

		return totalTrianglesSize;
	}

	void getDrawRanges(dae_reader_t *reader, int geometryID, vector<draw_range_t>& drawRange)
	{
		geometry_t *geometry = &reader->geometry[geometryID];
		elements_t elements = getElementOffsets(geometry);
		int drawRangeOffset = getBaseOffset(reader, geometryID);

		for (uint32_t meshes = 0; meshes < geometry->triangles.size(); meshes++)
		{
			vector<int>& triangles = geometry->triangles[meshes];
			int offset = triangles.size() / elements.elementsCount;
			draw_range_t range{ drawRangeOffset, (int)triangles.size() / elements.elementsCount };
			drawRange.push_back(range);
		}
	}

	int compareVertices(vector<float>& buf, int value0, int value1, uint32_t elementSize)
	{
		int v0 = elementSize * value0;
		int v1 = elementSize * value1;

		for (uint32_t n = 0; n < elementSize; n++)
		{
			if (buf[v0 + n] != buf[v1 + n])
				return 1;
		}

		return 0;
	}

	void fillIndices(dae_reader_t *reader, uint32_t geometryID, vector<float>& rawData)
	{
		geometry_t *geometry = &reader->geometry[geometryID];
		elements_t elements = getElementOffsets(geometry);
		int totalTrianglesSize = getTrianglesSize(geometry);
		vector<uint32_t>& indices = geometry->indices;
		int elementSize = getElementSize(reader);

		indices.resize(totalTrianglesSize / elements.elementsCount);
		memset(indices.data(), 0xFF, indices.size() * sizeof(uint32_t));

		int baseIdx = 0;

		for (uint32_t i = 0; i < geometryID; i++)
		{
			baseIdx += reader->geometry[i].maxIndex;
		}

		int idx = geometryID == 0 ? 0 : baseIdx + 1;

		for (uint32_t i = 0; i < indices.size(); i++)
		{
			if (indices[i] != -1) continue;

			indices[i] = idx;
			addVertex(rawData, geometry->bufferData, i*elementSize, elementSize);

			for (uint32_t j = i + 1; j < indices.size(); j++)
			{
				if (!compareVertices(rawData, i, j, elementSize))
				{
					indices[j] = idx;
				}
			}

			idx++;
		}

		geometry->maxIndex = idx - baseIdx - 1;
	}

	void computeIndices(dae_reader_t *reader, int geometryID)
	{
		geometry_t *geometry = &reader->geometry[geometryID];
		int totalTrianglesSize = getTrianglesSize(geometry);
		int arrayBufferSize = 3 * totalTrianglesSize;
		vector<float> rawData;
		rawData.reserve(arrayBufferSize);

		for (uint32_t meshes = 0; meshes < geometry->triangles.size(); meshes++)
		{
			fillTempBuffer(reader, geometry, meshes, rawData);
		}

		fillIndices(reader, geometryID, rawData);
	}

	void buildIndices(dae_reader_t *reader, int geometryID)
	{
		geometry_t *geometry = &reader->geometry[geometryID];
		vector<draw_range_t> drawRange;

		getDrawRanges(reader, geometryID, drawRange);
		geometry->meshes.push_back(drawRange);
		computeIndices(reader, geometryID);
	}
	
// namespace DAE_READER
}