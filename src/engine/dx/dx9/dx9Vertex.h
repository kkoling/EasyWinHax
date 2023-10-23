#pragma once
#include "..\..\rgb.h"
#include "..\..\..\vecmath.h"

// Struct to hold DirectX 9 Vertices used for drawing.

namespace hax {
	
	namespace dx9 {

		struct Vertex {
		private:
			Vector2 _coordinates;
			rgb::Color _color;

			Vertex() = delete;

		public:
			Vertex(Vector2 coordinates, hax::rgb::Color color) :
				_coordinates{ coordinates }, _color{ static_cast<rgb::Color>(UCHAR_A(color) << 24 | UCHAR_R(color) << 16 | UCHAR_G(color) << 8 | UCHAR_B(color)) } {}
		};

	}

}