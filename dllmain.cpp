#include <Windows.h>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <detours/detours.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl2.h>
#include <imgui/imgui_impl_win32.h>

#ifndef _DEBUG
#define PLOG_DISABLE_LOGGING
#endif

#ifdef _DEBUG
#include <iostream>
#include <plog/Log.h>
#include <plog/Logger.h>
#include <plog/Init.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Appenders/ColorConsoleAppender.h>
#endif

#define GET_OBJECT_VARIABLE_BY_OFFSET(variable_type, object_pointer, offset_in_bytes) *((variable_type*)(((unsigned int)(object_pointer)) + (offset_in_bytes)))

using namespace std;

LRESULT WINAPI CustomWindowProcCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
WNDPROC original_windowproc_callback = NULL;
void DrawImGui(void);
static bool imgui_show_menu;
static HANDLE game_mutex;

struct FVector
{
	float X;
	float Y;
	float Z;

	inline FVector() : X(0), Y(0), Z(0) {}

	inline FVector(float x, float y, float z) : X(x), Y(y), Z(z) {}

	inline FVector operator + (const FVector& other) const { return FVector(X + other.X, Y + other.Y, Z + other.Z); }

	inline FVector operator - (const FVector& other) const { return FVector(X - other.X, Y - other.Y, Z - other.Z); }

	inline FVector operator * (float scalar) const { return FVector(X * scalar, Y * scalar, Z * scalar); }

	inline FVector operator * (const FVector& other) const { return FVector(X * other.X, Y * other.Y, Z * other.Z); }

	inline FVector operator / (float scalar) const { return FVector(X / scalar, Y / scalar, Z / scalar); }

	inline FVector operator / (const FVector& other) const { return FVector(X / other.X, Y / other.Y, Z / other.Z); }

	inline FVector& operator=  (const FVector& other) { X = other.X; Y = other.Y; Z = other.Z; return *this; }

	inline FVector& operator+= (const FVector& other) { X += other.X; Y += other.Y; Z += other.Z; return *this; }

	inline FVector& operator-= (const FVector& other) { X -= other.X; Y -= other.Y; Z -= other.Z; return *this; }

	inline FVector& operator*= (const float other) { X *= other; Y *= other; Z *= other; return *this; }

	inline float Dot(const FVector& b) const { return (X * b.X) + (Y * b.Y) + (Z * b.Z); }

	inline float MagnitudeSqr() const { return Dot(*this); }

	inline float Magnitude() const { return std::sqrtf(MagnitudeSqr()); }

	inline FVector Unit() const
	{
		const float fMagnitude = Magnitude();
		return FVector(X / fMagnitude, Y / fMagnitude, Z / fMagnitude);
	}

	friend bool operator==(const FVector& first, const FVector& second) { return first.X == second.X && first.Y == second.Y && first.Z == second.Z; }

	friend bool operator!=(const FVector& first, const FVector& second) { return !(first == second); }

};
typedef FVector FRotator;
struct FVector2D
{
	float X;
	float Y;

	inline FVector2D() : X(0), Y(0) {}

	inline FVector2D(float x, float y) : X(x), Y(y) {}

	inline FVector2D operator + (const FVector2D& other) const { return FVector2D(X + other.X, Y + other.Y); }

	inline FVector2D operator - (const FVector2D& other) const { return FVector2D(X - other.X, Y - other.Y); }

	inline FVector2D operator * (float scalar) const { return FVector2D(X * scalar, Y * scalar); }

	inline FVector2D operator * (const FVector2D& other) const { return FVector2D(X * other.X, Y * other.Y); }

	inline FVector2D operator / (float scalar) const { return FVector2D(X / scalar, Y / scalar); }

	inline FVector2D operator / (const FVector2D& other) const { return FVector2D(X / other.X, Y / other.Y); }

	inline FVector2D& operator=  (const FVector2D& other) { X = other.X; Y = other.Y; return *this; }

	inline FVector2D& operator+= (const FVector2D& other) { X += other.X; Y += other.Y; return *this; }

	inline FVector2D& operator-= (const FVector2D& other) { X -= other.X; Y -= other.Y; return *this; }

	inline FVector2D& operator*= (const float other) { X *= other; Y *= other; return *this; }

	inline float Dot(const FVector2D& b) const {
		return (X * b.X) + (Y * b.Y);
	}

	inline float MagnitudeSqr() const {
		return Dot(*this);
	}

	inline float Magnitude() const {
		return std::sqrtf(MagnitudeSqr());
	}

	inline FVector2D Unit() const {
		const float fMagnitude = Magnitude();
		return FVector2D(X / fMagnitude, Y / fMagnitude);
	}

	friend bool operator==(const FVector2D& first, const FVector2D& second) {
		return first.X == second.X && first.Y == second.Y;
	}

	friend bool operator!=(const FVector2D& first, const FVector2D& second) {
		return !(first == second);
	}

};

struct RayInfo
{
	void* object;
	FVector point;
	FVector normal;
	unsigned int material;

	// Face and Face dot are currently only set by the extrudedPolyList
	// clipper.  Values are otherwise undefined.
	unsigned int face;                  // Which face was hit
	float faceDot;               // -Dot of face with poly normal
	float distance;
	// The collision struct has object, point, normal & material.
	float t;
};

class Timer {
private:
	std::chrono::steady_clock::time_point previous_tick_time_;
	float period_in_ms_;

public:
	void SetPeriod(float period_in_ms) {
		this->period_in_ms_ = period_in_ms;
	}

	void SetFrequency(float frequency) {
		this->period_in_ms_ = 1000 * (1.0 / frequency);
	}

	Timer(void) {
		this->previous_tick_time_ = std::chrono::steady_clock::time_point();
		SetPeriod(1000);
	}

	Timer(float frequency) : Timer() {
		SetFrequency(frequency);
	}

	bool IsReady(void) {
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		float delta = std::chrono::duration<float>(now - previous_tick_time_).count() * 1000.0;
		if (delta > period_in_ms_) {
			Tick(now);
			return true;
		}
		return false;
	}

	void Tick(void) {
		previous_tick_time_ = std::chrono::steady_clock::now();
	}

	void Tick(std::chrono::steady_clock::time_point now) {
		previous_tick_time_ = now;
	}

	void Reset(void) {
		previous_tick_time_ = std::chrono::steady_clock::time_point();
	}

	void Restart(void) {
		previous_tick_time_ = std::chrono::steady_clock::now();
	}
};

float* pitch = (float*)0x009E8E3C;
float* yaw = (float*)0x009E8268;
float* roll = (float*)0x009E88BC;

namespace imgui {

	namespace visuals {
		/* static */ enum MarkerStyle { kNone, kDot, kCircle, kFilledSquare, kSquare, kBounds, kFilledBounds };
		/* static */ const char* marker_labels[] = { "None", "Dot", "Circle", "Filled square", "Square", "Bounds", "Filled bounds" };

		struct Marker {
			MarkerStyle marker_style = MarkerStyle::kSquare;
			int marker_size = 5;
			int marker_thickness = 2;
			ImColor marker_colour = { 255, 255, 255, 255 };
		};

		/* static */ struct AimbotVisualSettings : Marker {
			bool scale_by_distance = true;
			int distance_for_scaling = 5000;
			int minimum_marker_size = 3;
			AimbotVisualSettings(void) {
				marker_style = MarkerStyle::kFilledBounds;
				marker_size = 9;
				marker_thickness = 2;
				marker_colour = { 255, 255, 0, 125 };
			}
		} aimbot_visual_settings;

		/* static */ struct RadarVisualSettings : Marker {
			ImColor enemy_marker_colour = { 255, 0, 0, 1 * 255 };
			ImColor friendly_marker_colour = { 0, 255, 0, 1 * 255 };
			ImColor enemy_flag_marker_colour = { 255, 255, 0, 255 };
			ImColor friendly_flag_marker_colour = { 0, 255, 255, 255 };
			ImColor window_background_colour = { 25, 25, 25, 25 };
			bool draw_axes = true;

			float zoom_ = 0.004 * 30;
			float zoom = 1;
			int window_size = 300 * 1.25;
			ImVec2 window_location = { 100, 100 };
			float border = 5;
			int axes_thickness = 1;
			RadarVisualSettings(void) {
				marker_style = MarkerStyle::kDot;
				marker_size = 3;
				marker_thickness = 1;
			}
		} radar_visual_settings;

		/* static */ struct ESPVisualSettings {
			struct BoundingBoxSettings {
				int box_thickness = 2;
				ImColor enemy_player_box_colour = { 0, 255, 255, 1 * 125 };
				ImColor friendly_player_box_colour = { 0, 255, 0, 1 * 125 };
			} bounding_box_settings;

			struct LineSettings {
				int line_thickness = 1;
				ImColor enemy_player_line_colour = { 255, 0, 0, 200 };
			} line_settings;

			struct NameSettings {
				ImColor enemy_name_colour = { 255, 255, 0, 1 * 255 };
				ImColor friendly_name_colour = { 0, 255, 0, 1 * 255 };
				float scale = 1.0;
				int name_height_offset = 30;
			} name_settings;

		} esp_visual_settings;

		/* static */ struct CrosshairSettings : Marker {
			bool enabled = true;

			CrosshairSettings(void) {
				marker_style = MarkerStyle::kDot;
				marker_size = 3;
				marker_thickness = 1;
				marker_colour = { 0, 255, 0, 255 };
			}
		} crosshair_settings;

		void DrawMarker(MarkerStyle marker_style, ImVec2 center, ImColor marker_colour, int marker_size, int marker_thickness) {
			ImDrawList* imgui_draw_list = ImGui::GetWindowDrawList();
			switch (marker_style) {
			case visuals::MarkerStyle::kDot:
				imgui_draw_list->AddCircleFilled(center, marker_size, marker_colour);
				break;
			case visuals::MarkerStyle::kCircle:
				imgui_draw_list->AddCircle(center, marker_size, marker_colour, 0, marker_thickness);
				break;
			case visuals::MarkerStyle::kFilledSquare:
				imgui_draw_list->AddRectFilled({ center.x - marker_size, center.y - marker_size }, { center.x + marker_size, center.y + marker_size }, marker_colour, 0);
				break;
			case visuals::MarkerStyle::kSquare:
				imgui_draw_list->AddRect({ center.x - marker_size, center.y - marker_size }, { center.x + marker_size, center.y + marker_size }, marker_colour, 0, 0, marker_thickness);
				break;
			default:
				break;
			}
		}

	}  // namespace visuals
	namespace imgui_menu {}

	/* static */ struct ImGuiSettings { } imgui_settings;
}  // namespace imgui

namespace math {
#define M_PI 3.14159265358979323846
#define M_2PI (2*M_PI)
#define M_PI_F ((float)(M_PI))
#define PI M_PI
#define DEG2RAD(x) ((float)(x) * (float)(M_PI_F / 180.f))
#define RAD2DEG(x) ((float)(x) * (float)(180.f / M_PI_F))
#define INV_PI (0.31830988618f)
#define HALF_PI (1.57079632679f)

	inline double clamp(double value, double min, double max) {
		if (value < min) {
			return min;
		}
		if (value > max) {
			return max;
		}
		return value;
	}

	inline void mSinCos(const float angle, float& s, float& c)
	{
		s = sin(angle);
		c = cos(angle);
	}

	inline float mDegToRad(float d)
	{
		return float((d * M_PI) / float(180));
	}


	class Matrix
	{
	public:
		float matrix_[16];

		operator float* () { return (matrix_); }              ///< Allow people to get at m.
		operator float* () const { return (float*)(matrix_); }  ///< Allow people to get at m.

		inline FVector GetColumn(unsigned int column) {
			return FVector(matrix_[column], matrix_[column + 4], matrix_[column + 8]);
		}

		inline void SetColumn(unsigned int column, FVector vector) {
			matrix_[column] = vector.X;
			matrix_[column + 4] = vector.Y;
			matrix_[column + 8] = vector.Z;
		}

		inline void Identity(float* m)
		{
			*m++ = 1.0f;
			*m++ = 0.0f;
			*m++ = 0.0f;
			*m++ = 0.0f;

			*m++ = 0.0f;
			*m++ = 1.0f;
			*m++ = 0.0f;
			*m++ = 0.0f;

			*m++ = 0.0f;
			*m++ = 0.0f;
			*m++ = 1.0f;
			*m++ = 0.0f;

			*m++ = 0.0f;
			*m++ = 0.0f;
			*m++ = 0.0f;
			*m = 1.0f;
		}

		inline Matrix& Mul(float* a, float* b) {
			float* mresult = *this;

			mresult[0] = a[0] * b[0] + a[1] * b[4] + a[2] * b[8] + a[3] * b[12];
			mresult[1] = a[0] * b[1] + a[1] * b[5] + a[2] * b[9] + a[3] * b[13];
			mresult[2] = a[0] * b[2] + a[1] * b[6] + a[2] * b[10] + a[3] * b[14];
			mresult[3] = a[0] * b[3] + a[1] * b[7] + a[2] * b[11] + a[3] * b[15];

			mresult[4] = a[4] * b[0] + a[5] * b[4] + a[6] * b[8] + a[7] * b[12];
			mresult[5] = a[4] * b[1] + a[5] * b[5] + a[6] * b[9] + a[7] * b[13];
			mresult[6] = a[4] * b[2] + a[5] * b[6] + a[6] * b[10] + a[7] * b[14];
			mresult[7] = a[4] * b[3] + a[5] * b[7] + a[6] * b[11] + a[7] * b[15];

			mresult[8] = a[8] * b[0] + a[9] * b[4] + a[10] * b[8] + a[11] * b[12];
			mresult[9] = a[8] * b[1] + a[9] * b[5] + a[10] * b[9] + a[11] * b[13];
			mresult[10] = a[8] * b[2] + a[9] * b[6] + a[10] * b[10] + a[11] * b[14];
			mresult[11] = a[8] * b[3] + a[9] * b[7] + a[10] * b[11] + a[11] * b[15];

			mresult[12] = a[12] * b[0] + a[13] * b[4] + a[14] * b[8] + a[15] * b[12];
			mresult[13] = a[12] * b[1] + a[13] * b[5] + a[14] * b[9] + a[15] * b[13];
			mresult[14] = a[12] * b[2] + a[13] * b[6] + a[14] * b[10] + a[15] * b[14];
			mresult[15] = a[12] * b[3] + a[13] * b[7] + a[14] * b[11] + a[15] * b[15];

			return *this;
		}

		inline Matrix& Set(float* e) {

			//Matrix result;

			float* result = *this;

			enum {
				AXIS_X = (1 << 0),
				AXIS_Y = (1 << 1),
				AXIS_Z = (1 << 2)
			};

			unsigned int axis = 0;
			if (e[0] != 0.0f) axis |= AXIS_X;
			if (e[1] != 0.0f) axis |= AXIS_Y;
			if (e[2] != 0.0f) axis |= AXIS_Z;

			switch (axis)
			{
			case 0:

				Identity(result);
				break;

			case AXIS_X:
			{
				float cx, sx;
				mSinCos(e[0], sx, cx);

				result[0] = 1.0f;
				result[1] = 0.0f;
				result[2] = 0.0f;
				result[3] = 0.0f;

				result[4] = 0.0f;
				result[5] = cx;
				result[6] = sx;
				result[7] = 0.0f;

				result[8] = 0.0f;
				result[9] = -sx;
				result[10] = cx;
				result[11] = 0.0f;

				result[12] = 0.0f;
				result[13] = 0.0f;
				result[14] = 0.0f;
				result[15] = 1.0f;
				break;
			}

			case AXIS_Y:
			{
				float cy, sy;
				mSinCos(e[1], sy, cy);

				result[0] = cy;
				result[1] = 0.0f;
				result[2] = -sy;
				result[3] = 0.0f;

				result[4] = 0.0f;
				result[5] = 1.0f;
				result[6] = 0.0f;
				result[7] = 0.0f;

				result[8] = sy;
				result[9] = 0.0f;
				result[10] = cy;
				result[11] = 0.0f;

				result[12] = 0.0f;
				result[13] = 0.0f;
				result[14] = 0.0f;
				result[15] = 1.0f;
				break;
			}

			case AXIS_Z:
			{
				// the matrix looks like this:
				//  r1 - (r4 * sin(x))     r2 + (r3 * sin(x))   -cos(x) * sin(y)
				//  -cos(x) * sin(z)       cos(x) * cos(z)      sin(x)
				//  r3 + (r2 * sin(x))     r4 - (r1 * sin(x))   cos(x) * cos(y)
				//
				// where:
				//  r1 = cos(y) * cos(z)
				//  r2 = cos(y) * sin(z)
				//  r3 = sin(y) * cos(z)
				//  r4 = sin(y) * sin(z)
				float cz, sz;
				mSinCos(e[2], sz, cz);
				float r1 = cz;
				float r2 = sz;
				float r3 = 0.0f;
				float r4 = 0.0f;

				result[0] = cz;
				result[1] = sz;
				result[2] = 0.0f;
				result[3] = 0.0f;

				result[4] = -sz;
				result[5] = cz;
				result[6] = 0.0f;
				result[7] = 0.0f;

				result[8] = 0.0f;
				result[9] = 0.0f;
				result[10] = 1.0f;
				result[11] = 0.0f;

				result[12] = 0.0f;
				result[13] = 0.0f;
				result[14] = 0.0f;
				result[15] = 1.0f;
				break;
			}

			default:
				// the matrix looks like this:
				//  r1 - (r4 * sin(x))     r2 + (r3 * sin(x))   -cos(x) * sin(y)
				//  -cos(x) * sin(z)       cos(x) * cos(z)      sin(x)
				//  r3 + (r2 * sin(x))     r4 - (r1 * sin(x))   cos(x) * cos(y)
				//
				// where:
				//  r1 = cos(y) * cos(z)
				//  r2 = cos(y) * sin(z)
				//  r3 = sin(y) * cos(z)
				//  r4 = sin(y) * sin(z)
				float cx, sx;
				mSinCos(e[0], sx, cx);
				float cy, sy;
				mSinCos(e[1], sy, cy);
				float cz, sz;
				mSinCos(e[2], sz, cz);
				float r1 = cy * cz;
				float r2 = cy * sz;
				float r3 = sy * cz;
				float r4 = sy * sz;

				result[0] = r1 - (r4 * sx);
				result[1] = r2 + (r3 * sx);
				result[2] = -cx * sy;
				result[3] = 0.0f;

				result[4] = -cx * sz;
				result[5] = cx * cz;
				result[6] = sx;
				result[7] = 0.0f;

				result[8] = r3 + (r2 * sx);
				result[9] = r4 - (r1 * sx);
				result[10] = cx * cy;
				result[11] = 0.0f;

				result[12] = 0.0f;
				result[13] = 0.0f;
				result[14] = 0.0f;
				result[15] = 1.0f;
				break;
			}

			return *this;
		}
	};

	FVector CrossProduct(FVector U, FVector V) {
		return FVector(U.Y * V.Z - U.Z * V.Y, U.Z * V.X - U.X * V.Z, U.X * V.Y - U.Y * V.X);
	}

	bool IsVectorToRight(FVector base_vector, FVector vector_to_check) {
		FVector right = CrossProduct(base_vector, { 0, 0, 1 });

		if (right.Dot(vector_to_check) < 0) {
			return true;
		}
		else {
			return false;
		}
	}

	// Angle on the X-Y plane
	float AngleBetweenVector(FVector a, FVector b) {
		// a.b = |a||b|cosx
		a.Z = 0;
		b.Z = 0;

		float dot = a.Dot(b);
		float denom = a.Magnitude() * b.Magnitude();
		float div = dot / denom;
		return acos(div);
	}
}

namespace hooks {
	typedef LRESULT(__stdcall* SetWindowLongPtr_)(HWND, int, long);
	SetWindowLongPtr_ OriginalSetWindowLongPtr = NULL;
	LRESULT __stdcall SetWindowLongPtrHook(HWND hWnd, int arg1, long arg2);

	typedef BOOL(__stdcall* wglSwapBuffers)(int*);
	wglSwapBuffers OriginalwglSwapBuffers = NULL;
	BOOL __stdcall wglSwapBuffersHook(int* arg1);

	double model_matrix[16], proj_matrix[16];
	int viewport_[4];
	typedef int(__stdcall* GluProject)(double objx, double objy, double objz, const double modelMatrix[16], const double projMatrix[16], const int viewport[4], double* winx, double* winy, double* winz);
	GluProject OriginalGluProject;
	int __stdcall GluProjectHook(double objx, double objy, double objz, const double modelMatrix[16], const double projMatrix[16], const int viewport[4], double* winx, double* winy, double* winz);

	typedef void(__cdecl* FpsUpdate)(void);
	FpsUpdate OriginalFpsUpdate = (FpsUpdate)0x00564570;
	void FpsUpdateHook(void);

	void* container;
	typedef bool(__thiscall* CastRay)(void* this_container, FVector& a2, FVector& a3, unsigned int a4, RayInfo* a5);
	CastRay OriginalCastRay = (CastRay)0x0058D900;
	bool __fastcall CastRayHook(void* this_container, void* _, FVector& a2, FVector& a3, unsigned int a4, RayInfo* a5);

	typedef void(__thiscall* PlayerSetRenderPosition)(void*, void*, void*, void*);
	PlayerSetRenderPosition OriginalPlayerSetRenderPosition = (PlayerSetRenderPosition)0x005D98C0;
	void __fastcall SetRenderPositionHook(void* this_player, void* _, void* arg1, void* arg2, void* arg3);

	typedef void(__thiscall* GetEyeTransform)(void*, math::Matrix*);
	GetEyeTransform OriginalGetEyeTransform = (GetEyeTransform)0x005D9AA0;

	typedef void(__thiscall* GetMuzzlePoint)(void*, int, FVector*);
	GetMuzzlePoint OriginalGetMuzzlePoint = (GetMuzzlePoint)0x005F7380;

	typedef void(__thiscall* SetImage)(void* shape_base, unsigned int imageSlot, void* imageData, void* skinNameHandle, bool loaded, bool ammo, bool triggerDown, bool target);
	void __fastcall SetImageHook(void* this_shapebase, void* _, unsigned int imageSlot, void* imageData, void* skinNameHandle, bool loaded, bool ammo, bool triggerDown, bool target);
	SetImage OriginalSetImage = (SetImage)0x005F8200;

	typedef void(__cdecl* SetWindowLocked)(bool);
	SetWindowLocked OriginalSetWindowLocked = (SetWindowLocked)0x005600A0;
}

namespace game_data {

	/* static */ FVector2D screen_size = FVector2D();
	/* static */ FVector2D screen_center = FVector2D();

	enum class Weapon { none, disc, cg, gl, blaster, plasma, sniper, shocklance, unknown };
	enum class WeaponType { kHitscan, kProjectileLinear, kProjectileArching };

	namespace information {

		/* static */ struct WeaponSpeeds {
			struct Weapon {
				float bullet_speed;
				float inheritence;
			};

			struct disc : Weapon {
				disc() {
					bullet_speed = 95;
					inheritence = 0.75;
				}
			} disc;

			struct Chaingun : Weapon {
				Chaingun() {
					bullet_speed = 425;
					inheritence = 0.75;
				}
			} chaingun;

			struct Grenadelauncher : Weapon {
				Grenadelauncher() {
					bullet_speed = 0;
					inheritence = 0.75;
				}
			} grenadelauncher;

			struct Plasma : Weapon {
				Plasma() {
					bullet_speed = 55;
					inheritence = 0.3;
				}
			} plasma;

			struct Blaster : Weapon {
				Blaster() {
					bullet_speed = 90;
					inheritence = 0.5;
				}
			} blaster;
		} weapon_speeds;

		struct GameActor {
		public:
			int team_id_ = -1;
			FVector location_;
			FRotator rotation_;
			FVector velocity_;
			// FVector velocity_previous_;
			FVector forward_vector_;
			FVector acceleration_;

			bool IsSameTeam(GameActor* actor) {
				return this->team_id_ == actor->team_id_;
			}
		};

		struct Player : public GameActor {
		public:
			void* character_ = NULL;

			int player_id_ = -1;
			float health_ = 0;
			float energy_ = 0;

			bool is_valid_ = false;
			Weapon weapon_ = Weapon::none;
			WeaponType weapon_type_ = WeaponType::kHitscan;

			wstring name_w_;
			string name_c_;

			FVector eye_;

			void Reset(void) {
				character_ = NULL;
				is_valid_ = false;
			}

			void Setup(void* character) {
				Reset();
				if (!character)
					return;

				player_id_ = GET_OBJECT_VARIABLE_BY_OFFSET(unsigned int, character, 32);

				if (GET_OBJECT_VARIABLE_BY_OFFSET(int, character, 512 * 4) || !GET_OBJECT_VARIABLE_BY_OFFSET(unsigned int, character, 0x26C)) {
					return;
				}

				team_id_ = GET_OBJECT_VARIABLE_BY_OFFSET(int, GET_OBJECT_VARIABLE_BY_OFFSET(unsigned int, character, 0x26C), 0x18);

				math::Matrix matrix = GET_OBJECT_VARIABLE_BY_OFFSET(math::Matrix, character, 156);
				FVector player_bottom = matrix.GetColumn(3);

				//hooks::OriginalGetEyeTransform(character, &matrix); <- causes a crash for players other than the client player
				//eye_ = matrix.GetColumn(3);

				void* player_data = GET_OBJECT_VARIABLE_BY_OFFSET(void*, character, 648 * 4);
				FVector player_bounding_box = GET_OBJECT_VARIABLE_BY_OFFSET(FVector, player_data, 1320);
				eye_ = player_bottom;
				eye_.Z += player_bounding_box.Z;

				location_ = FVector(eye_.X, eye_.Y, (eye_.Z + player_bottom.Z) / 2);
				velocity_ = GET_OBJECT_VARIABLE_BY_OFFSET(FVector, character, 2392);
				forward_vector_ = matrix.GetColumn(1);

				FVector rot_ = GET_OBJECT_VARIABLE_BY_OFFSET(FVector, character, 2380);
				FVector head_ = GET_OBJECT_VARIABLE_BY_OFFSET(FVector, character, 2368);

				rotation_ = FVector(head_.X, 0, rot_.Z);

				//location_ = character->RootComponent->RelativeLocation;
				//rotation_ = character->RootComponent->RelativeRotation;
				//velocity_ = character->RootComponent->ComponentVelocity;
				// acceleration_ = character->CharacterMovement->Acceleration;
				//forward_vector_ = math::RotatorToVector(rotation_).Unit();

				character_ = character;

				is_valid_ = true;

				/*
				if (character->PlayerState->PlayerName.IsValid())
					name_w_ = wstring(character->PlayerState->PlayerName.cw_str());
				else
					name_w_ = L"";
				name_c_ = string(name_w_.begin(), name_w_.end());
				*/
			}
		};
	}  // namespace information

	/* static */ class GameData {
	public:
		information::Player my_player_information;
		vector<information::Player> players;

		GameData(void) {
			;
		}

		void Reset(void) {
			players.clear();

			//my_player_information.weapon_ = Weapon::none;
			//my_player_information.weapon_type_ = WeaponType::kHitscan;
			my_player_information.is_valid_ = false;  // Invalidate the player every frame
		}

	} game_data;

	/* static */ game_data::information::Player& my_player = game_data::game_data.my_player_information;

	///* static */ Timer get_player_controllers_timer(0.1 * 5);

	void GetPlayers(void) {
		return;
	}

	void GetGameData(void);

}  // namespace game_data

namespace game_functions {
	bool InLineOfSight(game_data::information::GameActor* actor) {
		static RayInfo ray_info;
		FVector start = game_data::my_player.eye_;
		start += game_data::my_player.forward_vector_.Unit() * 4;
		bool line_of_sight = hooks::OriginalCastRay(hooks::container, start, actor->location_, 0xFFFFFFFF, &ray_info);
		if (ray_info.object) {
			void* namespace_ = GET_OBJECT_VARIABLE_BY_OFFSET(void*, ray_info.object, 36);
			if (namespace_) {
				char* namespace_name_ = GET_OBJECT_VARIABLE_BY_OFFSET(char*, namespace_, 0);
				if (std::string(namespace_name_) == "Player") {
					return true;
				}
			}
		}
		return false;
	}

	// float fovv = 120;
	FVector2D Project(FVector location) {
		double x, y, z;
		if (hooks::OriginalGluProject(location.X, location.Y, location.Z, hooks::model_matrix, hooks::proj_matrix, hooks::viewport_, &x, &y, &z)) {
			if (z >= 0 && z <= 1) {
				return FVector2D(x, y);
			}
		}
		return FVector2D(0, 0);
	}

	bool IsInFieldOfView(FVector enemy_location) {
		FVector rotation_vector = game_data::my_player.forward_vector_;
		FVector difference_vector = enemy_location - game_data::my_player.location_;
		double dot = rotation_vector.X * difference_vector.X + rotation_vector.Y * difference_vector.Y;
		if (dot <= 0)      // dot is > 0 if vectors face same direction, 0 if vectors perpendicular, negative if facing oppposite directions
			return false;  // we want to ensure the vectors face in the same direction
		return true;
	}

	bool IsInHorizontalFieldOfView(FVector enemy_location, double horizontal_fov) {
		FVector rotation_vector = game_data::my_player.forward_vector_;
		FVector difference_vector = enemy_location - game_data::my_player.location_;
		double dot = rotation_vector.X * difference_vector.X + rotation_vector.Y * difference_vector.Y;
		if (dot <= 0)      // dot is > 0 if vectors face same direction, 0 if vectors perpendicular, negative if facing oppposite directions
			return false;  // we want to ensure the vectors face in the same direction

		double dot_sq = dot * dot;  // squaring the dot loses the negative sign, so we cant tell from this point onwards
									// if the enemy is in front or behind us if using dot_sq
		double denom = (rotation_vector.X * rotation_vector.X + rotation_vector.Y * rotation_vector.Y) * (difference_vector.X * difference_vector.X + difference_vector.Y * difference_vector.Y);
		double angle_sq = dot_sq / denom;
		double v = pow(cos(DEG2RAD(horizontal_fov)), 2);

		if (angle_sq < v)
			return false;
		return true;
	}

	FVector GetMuzzleOffset(void) {
		FVector muzzle_point;
		hooks::OriginalGetMuzzlePoint(game_data::my_player.character_, 0, &muzzle_point);
		return muzzle_point - game_data::my_player.location_;
	}

}  // namespace game_functions

namespace other {

	/* static */ struct OtherSettings { bool disable_hitmarker = false; } other_settings;

	void SendLeftMouseClick(void) {
		INPUT inputs[2];
		ZeroMemory(inputs, sizeof(inputs));

		inputs[0].type = INPUT_MOUSE;
		inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

		inputs[1].type = INPUT_MOUSE;
		inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;

		UINT uSent = SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
	}
}  // namespace other

namespace esp {

	/* static */ struct ESPSettings {
		bool enabled = true;
		int poll_frequency = 60 * 5;
		bool show_friendlies = false;
		//int player_height = 100;
		int player_width = 0;
		float width_to_height_ratio = 0.5;
		bool show_lines = true;
		bool show_names = false;
	} esp_settings;

	/* static */ Timer get_esp_data_timer(esp_settings.poll_frequency);

	struct ESPInformation {
		FVector2D projection;  // center
		float height;          // height for box/rectangle
		bool is_friendly = false;
		string name;
	};

	vector<ESPInformation> esp_information;

	void Tick(void) {
		if (!esp_settings.enabled || !get_esp_data_timer.IsReady())
			return;

		esp_information.clear();

		if (!game_data::my_player.is_valid_)
			;  // return;

		// bool is_my_player_alive = game_data::my_player.is_valid_;
		for (vector<game_data::information::Player>::iterator player = game_data::game_data.players.begin(); player != game_data::game_data.players.end(); player++) {
			if (!player->is_valid_) {
				continue;
			}

			game_data::information::Player* p = (game_data::information::Player*)&*player;
			bool same_team = game_data::my_player.IsSameTeam(p);

			if ((same_team && !esp_settings.show_friendlies) || !game_functions::IsInFieldOfView(player->location_))
				continue;

			if (!game_functions::IsInFieldOfView(player->location_))
				continue;

			FVector2D center_projection = game_functions::Project(player->location_);
			player->location_.Z += (player->eye_.Z - player->location_.Z);  // this is HALF the height in reality
			FVector2D head_projection = game_functions::Project(player->location_);
			player->location_.Z -= (player->eye_.Z - player->location_.Z);  // this is HALF the height in reality
			float height = abs(head_projection.Y - center_projection.Y);
			float width = esp_settings.width_to_height_ratio * height;
			esp_information.push_back({ center_projection, height, same_team, player->name_c_ });

			// cout << height << endl;
		}
	}
}  // namespace esp

namespace aimbot {

	// Overshooting means the weapon bullet speed is too low
	// Undershooting means the weapon bullet speed is too high

	/* static */ float delta_time = 0;
	vector<game_data::information::Player> players_previous;

	enum AimbotMode { kClosestDistance, kClosestXhair };
	/* static */ const char* mode_labels[] = { "Closest distance", "Closest to crosshair" };
	/* static */ bool enabled = true;

	/* static */ struct AimbotSettings {
		AimbotMode aimbot_mode = AimbotMode::kClosestXhair;

		bool enabled = true;          // enabling really just enables aimassist, this isnt really an aimbot
		bool enabled_aimbot = false;
		bool target_everyone = true;  // if we want to do prediction on every single player

		float disc_ping_in_ms = 0;   //-90
		float chaingun_ping_in_ms = 0;  //-50
		float grenadelauncher_ping_in_ms = 0;
		float plasmagun_ping_in_ms = 0;
		float blaster_ping_in_ms = 0;

		bool stay_locked_to_target = true;
		bool auto_lock_to_new_target = false;

		float aimbot_horizontal_fov_angle = 90;         // 30;
		float aimbot_horizontal_fov_angle_cos = 0;      // 0.86602540378;
		float aimbot_horizontal_fov_angle_cos_sqr = 0;  // 0.75;

		bool friendly_fire = false;
		bool need_line_of_sight = true;

		int aimbot_poll_frequency = 60 * 1;

		bool use_acceleration = false;
		bool use_acceleration_cg_only = false;
		// float acceleration_delta_in_ms = 30;

		bool triggerbot_enabled = false;
	} aimbot_settings;

	/* static */ Timer aimbot_poll_timer(aimbot_settings.aimbot_poll_frequency);

	/* static */ game_data::information::Player target_player;

	vector<FVector2D> projections_of_predictions;

	struct AimbotInformation {
		float distance_;
		FVector2D projection_;
		float height;
	};

	vector<AimbotInformation> aimbot_information;

	/* static */ struct WeaponAimbotParameters {
		int maximum_iterations = 2 * 10;
		float epsilon = 0.05 / 3;
	} aimbot_parameters_;

	bool PredictAimAtTarget(game_data::information::Player* target_player, FVector* output_vector, FVector offset) {
		float projectileSpeed;
		float inheritence;
		float ping;

		switch (game_data::my_player.weapon_) {
			using namespace game_data::information;
		case game_data::Weapon::disc:
			projectileSpeed = weapon_speeds.disc.bullet_speed;
			inheritence = weapon_speeds.disc.inheritence;
			ping = aimbot::aimbot_settings.disc_ping_in_ms;
			break;
		case game_data::Weapon::cg:
			projectileSpeed = weapon_speeds.chaingun.bullet_speed;
			inheritence = weapon_speeds.chaingun.inheritence;
			ping = aimbot::aimbot_settings.chaingun_ping_in_ms;
			break;
		case game_data::Weapon::gl:
			projectileSpeed = weapon_speeds.grenadelauncher.bullet_speed;
			inheritence = weapon_speeds.grenadelauncher.inheritence;
			ping = aimbot::aimbot_settings.grenadelauncher_ping_in_ms;
			break;
		case game_data::Weapon::plasma:
			projectileSpeed = weapon_speeds.plasma.bullet_speed;
			inheritence = weapon_speeds.plasma.inheritence;
			ping = aimbot::aimbot_settings.plasmagun_ping_in_ms;
			break;
		case game_data::Weapon::blaster:
			projectileSpeed = weapon_speeds.blaster.bullet_speed;
			inheritence = weapon_speeds.blaster.inheritence;
			ping = aimbot::aimbot_settings.blaster_ping_in_ms;
			break;
		case game_data::Weapon::shocklance:
		case game_data::Weapon::sniper:
			break;
		case game_data::Weapon::none:
		case game_data::Weapon::unknown:
		default:
			return false;
		}

		if (game_data::my_player.weapon_type_ == game_data::WeaponType::kHitscan) {
			*output_vector = target_player->location_;
			return true;
		}


		if (game_data::my_player.weapon_type_ == game_data::WeaponType::kProjectileArching) {
			return false;
		}


		if (game_data::my_player.weapon_type_ != game_data::WeaponType::kProjectileArching && game_data::my_player.weapon_type_ != game_data::WeaponType::kProjectileLinear)
			return false;

		FVector owner_location = game_data::my_player.location_ - offset * 1;
		FVector owner_velocity = game_data::my_player.velocity_;

		// owner_location = owner_location - owner_velocity * (weapon_parameters_.self_compensation_ping_ / 1000.0);

		FVector target_location = target_player->location_;
		FVector target_velocity = target_player->velocity_;

		/*
		a_ = (v-u)/t
		acceleration is normalised -> a = a_ / |a_|
		*/
		FVector target_acceleration = FVector();
		if (aimbot::aimbot_settings.use_acceleration && (!aimbot_settings.use_acceleration_cg_only || (aimbot_settings.use_acceleration_cg_only && (game_data::game_data.my_player_information.weapon_ == game_data::Weapon::cg || game_data::game_data.my_player_information.weapon_ == game_data::Weapon::blaster)))) {
			FVector velocity_previous = FVector();
			bool player_found = false;
			for (vector<game_data::information::Player>::iterator player = players_previous.begin(); player != players_previous.end(); player++) {
				if (player->character_ == target_player->character_) {
					player_found = true;
					velocity_previous = player->velocity_;
					break;
				}
			}

			if (player_found) {
				target_acceleration = (target_velocity - velocity_previous) / (delta_time / 1000.0);  // should be dividing by 1000 to get ms in seconds  //(aimbot::aimbot_settings.acceleration_delta_in_ms/1000.0);
			}
		}

		FVector target_ping_prediction = target_location + (target_velocity * ping / 1000.0 * 0) + (target_acceleration * pow(ping / 1000.0, 2) * 0.5 * 0);
		FVector prediction = target_ping_prediction;
		FVector ping_prediction = target_ping_prediction;

		/* static */ vector<double> D(aimbot_parameters_.maximum_iterations, 0);
		/* static */ vector<double> dt(aimbot_parameters_.maximum_iterations, 0);

		int i = 0;
		do {
			D[i] = (owner_location - prediction).Magnitude();
			dt[i] = D[i] / projectileSpeed;
			if (i > 0 && abs(dt[i] - dt[i - 1]) < aimbot_parameters_.epsilon) {
				break;
			}

			prediction = target_ping_prediction + (target_velocity * dt[i] * 1) + (target_acceleration * pow(dt[i], 2) * 0.5) - (owner_velocity * inheritence * dt[i]);
			i++;
		} while (i < aimbot_parameters_.maximum_iterations);

		if (i == aimbot_parameters_.maximum_iterations) {
			return false;
		}

		dt[i] += ping / 1000.0;
		ping_prediction = prediction = target_ping_prediction + (target_velocity * dt[i] * 1) + (target_acceleration * pow(dt[i], 2) * 0.5) - (owner_velocity * inheritence * dt[i]);
		*output_vector = ping_prediction;

		return true;
	}

	void Enable(void) {
		enabled = true;
	}

	void Reset(void) {
		target_player.Reset();
		Enable();
	}

	void Disable(void) {
		Reset();
		enabled = false;
	}

	void Toggle(void) {
		aimbot_settings.enabled = !aimbot_settings.enabled;
		if (!aimbot_settings.enabled) {
			Reset();
		}
		return;

		if (enabled) {
			Disable();
		}
		else {
			Enable();
		}
	}

	bool FindTarget(void) {
		void* current_target_character = target_player.character_;
		target_player.Setup(current_target_character);

		bool need_to_find_target = true;  //! aimbot_settings.target_everyone;

		if (aimbot_settings.stay_locked_to_target) {
			if (!target_player.is_valid_) {
				if (!aimbot_settings.auto_lock_to_new_target && current_target_character != NULL) {
					Disable();
					return false;
				}
			}
			else {
				need_to_find_target = false;
			}
		}

		if (need_to_find_target) {
			current_target_character = NULL;
			FVector rotation_vector = game_data::my_player.forward_vector_;
			for (vector<game_data::information::Player>::iterator player = game_data::game_data.players.begin(); player != game_data::game_data.players.end(); player++) {
				if (!player->is_valid_) {
					continue;
				}

				game_data::information::Player* p = (game_data::information::Player*)&*player;
				bool same_team = game_data::my_player.IsSameTeam(p);
				bool line_of_sight = game_functions::InLineOfSight(p);
				if ((same_team && !aimbot_settings.friendly_fire) || (!game_functions::IsInFieldOfView(player->location_) && aimbot_settings.aimbot_mode == AimbotMode::kClosestXhair) || (!line_of_sight && aimbot_settings.need_line_of_sight))
					continue;

				switch (aimbot_settings.aimbot_mode) {
				case AimbotMode::kClosestXhair: {
					static double distance = 0;
					FVector enemy_location = player->location_;

					if (!game_functions::IsInHorizontalFieldOfView(player->location_, aimbot_settings.aimbot_horizontal_fov_angle))
						continue;

					FVector2D projection_vector = game_functions::Project(enemy_location);
					if (!current_target_character) {
						current_target_character = player->character_;
						distance = (projection_vector - game_data::screen_center).MagnitudeSqr();
					}
					else {
						double current_distance = (projection_vector - game_data::screen_center).MagnitudeSqr();
						if (current_distance < distance) {
							current_target_character = player->character_;
							distance = current_distance;
						}
					}
				} break;

				case AimbotMode::kClosestDistance: {
					static double distance = 0;
					if (!current_target_character) {
						current_target_character = player->character_;
						distance = (game_data::my_player.location_ - player->location_).MagnitudeSqr();
					}
					else {
						double current_distance = (game_data::my_player.location_ - player->location_).MagnitudeSqr();
						if (current_distance < distance) {
							current_target_character = player->character_;
							distance = current_distance;
						}
					}
				} break;
				}
			}
		}

		target_player.Setup(current_target_character);

		return current_target_character != NULL;
	}

	/* static */ std::chrono::steady_clock::time_point previous_tick = std::chrono::steady_clock::now();

	void Tick(void) {
		if (!aimbot_settings.enabled /*|| !enabled*/ || !aimbot_poll_timer.IsReady())
			return;

		// static std::chrono::steady_clock::time_point previous_tick = std::chrono::steady_clock::now();
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		delta_time = std::chrono::duration<float>(now - previous_tick).count() * 1000.0;
		previous_tick = now;

		projections_of_predictions.clear();
		aimbot_information.clear();

		if (!game_data::my_player.is_valid_ || game_data::my_player.weapon_ == game_data::Weapon::none || game_data::my_player.weapon_ == game_data::Weapon::unknown)
			return;

		/* static */ FVector prediction;
		FVector muzzle_offset = game_functions::GetMuzzleOffset();

		if (!aimbot_settings.target_everyone) {
			if (enabled && FindTarget() /*&& target_player.character_*/) {
				bool result = PredictAimAtTarget(&target_player, &prediction, muzzle_offset);

				if (result && game_functions::IsInFieldOfView(prediction)) {
					FVector2D projection = game_functions::Project(prediction);
					float height = -1;

					if (((imgui::visuals::aimbot_visual_settings.marker_style == imgui::visuals::MarkerStyle::kBounds || imgui::visuals::aimbot_visual_settings.marker_style == imgui::visuals::MarkerStyle::kFilledBounds) && height == -1) || aimbot_settings.triggerbot_enabled) {
						FVector2D center_projection = game_functions::Project(target_player.location_);
						target_player.location_.Z += (target_player.eye_.Z - target_player.location_.Z);  // this is HALF the height in reality
						FVector2D head_projection = game_functions::Project(target_player.location_);
						target_player.location_.Z -= (target_player.eye_.Z - target_player.location_.Z);  // this is HALF the height in reality
						height = abs(head_projection.Y - center_projection.Y);

						if (aimbot_settings.triggerbot_enabled) {
							float width = esp::esp_settings.width_to_height_ratio * height;
							if (abs(game_data::screen_center.X - projection.X) < width && abs(game_data::screen_center.Y - projection.Y) < height) {
								if (game_data::my_player.weapon_ == game_data::Weapon::disc || game_data::my_player.weapon_ == game_data::Weapon::gl || game_data::my_player.weapon_ == game_data::Weapon::plasma || game_data::my_player.weapon_ == game_data::Weapon::blaster) {
									other::SendLeftMouseClick();
								}
							}
						}
					}

					projections_of_predictions.push_back(projection);
					aimbot_information.push_back({ (prediction - game_data::my_player.location_).Magnitude(), projection, height });

					if (aimbot::aimbot_settings.enabled_aimbot) {
						float xDiff = prediction.X - game_data::my_player.location_.X;
						float yDiff = prediction.Y - game_data::my_player.location_.Y;

						FVector rot = GET_OBJECT_VARIABLE_BY_OFFSET(FVector, game_data::my_player.character_, 2380);
						FVector head = GET_OBJECT_VARIABLE_BY_OFFSET(FVector, game_data::my_player.character_, 2368);

						float curYaw = rot.Z;
						while (curYaw > M_2PI)
							curYaw -= M_2PI;
						while (curYaw < -M_2PI)
							curYaw += M_2PI;

						// find the yaw offset
						float newYaw = atan2(xDiff, yDiff);
						float yawDiff = newYaw - curYaw;

						// make it between 0 and 2PI
						if (yawDiff < 0.0f)
							yawDiff += M_2PI;
						else if (yawDiff >= M_2PI)
							yawDiff -= M_2PI;

						// now make sure we take the short way around the circle
						if (yawDiff > M_PI)
							yawDiff -= M_2PI;
						else if (yawDiff < -M_PI)
							yawDiff += M_2PI;

						*yaw = yawDiff;

						// Next do pitch.
						float vertDist = prediction.Z - game_data::my_player.eye_.Z;
						float horzDist = sqrt(xDiff * xDiff + yDiff * yDiff);
						float newPitch = atan2(horzDist, vertDist) - (M_PI / 2.0f);
						if (fabs(newPitch) > 0.01 || true) {
							*pitch = newPitch - head.X;
						}
					}
				}
			}
		}
		else {
			for (vector<game_data::information::Player>::iterator player = game_data::game_data.players.begin(); player != game_data::game_data.players.end(); player++) {
				if (!player->is_valid_ || player->character_ == game_data::my_player.character_) {
					continue;
				}

				game_data::information::Player* p = (game_data::information::Player*)&*player;
				bool same_team = game_data::my_player.IsSameTeam(p);
				bool line_of_sight = game_functions::InLineOfSight(p);

				if ((same_team && !aimbot_settings.friendly_fire) || (!game_functions::IsInFieldOfView(player->location_) && aimbot_settings.aimbot_mode == AimbotMode::kClosestXhair) || (!line_of_sight && aimbot_settings.need_line_of_sight))
					continue;

				if (!game_functions::IsInHorizontalFieldOfView(player->location_, aimbot_settings.aimbot_horizontal_fov_angle))
					continue;

				// aimbot::aimbot_settings.use_acceleration = false;

				bool result = PredictAimAtTarget(&*p, &prediction, muzzle_offset);

				if (result && game_functions::IsInFieldOfView(prediction)) {
					FVector2D projection = game_functions::Project(prediction);
					float height = -1;

					if (((imgui::visuals::aimbot_visual_settings.marker_style == imgui::visuals::MarkerStyle::kBounds || imgui::visuals::aimbot_visual_settings.marker_style == imgui::visuals::MarkerStyle::kFilledBounds) && height == -1) || aimbot_settings.triggerbot_enabled) {
						FVector2D center_projection = game_functions::Project(player->location_);
						player->location_.Z += (player->eye_.Z - player->location_.Z);  // this is HALF the height in reality
						FVector2D head_projection = game_functions::Project(player->location_);
						player->location_.Z -= (player->eye_.Z - player->location_.Z);  // this is HALF the height in reality
						height = abs(head_projection.Y - center_projection.Y);

						if (aimbot_settings.triggerbot_enabled) {
							float width = esp::esp_settings.width_to_height_ratio * height;
							if (abs(game_data::screen_center.X - projection.X) < width && abs(game_data::screen_center.Y - projection.Y) < height) {
								if (game_data::my_player.weapon_ == game_data::Weapon::disc || game_data::my_player.weapon_ == game_data::Weapon::gl || game_data::my_player.weapon_ == game_data::Weapon::plasma || game_data::my_player.weapon_ == game_data::Weapon::blaster) {
									other::SendLeftMouseClick();
								}
							}
						}
					}

					projections_of_predictions.push_back(projection);
					aimbot_information.push_back({ (prediction - game_data::my_player.location_).Magnitude(), projection, height });
				}

				/*
				aimbot::aimbot_settings.use_acceleration = true;

				result = PredictAimAtTarget(&*p, &prediction, muzzle_offset);

				if (result) {
					FVector2D projection = game_functions::Project(prediction);
					float height = -1;

					if ((imgui::visuals::aimbot_visual_settings.marker_style == imgui::visuals::MarkerStyle::kBounds || imgui::visuals::aimbot_visual_settings.marker_style == imgui::visuals::MarkerStyle::kFilledBounds) && height == -1) {
						FVector2D center_projection = game_functions::Project(player->location_);
						player->location_.Z += esp::esp_settings.player_height;  // this is HALF the height in reality
						FVector2D head_projection = game_functions::Project(player->location_);
						player->location_.Z -= esp::esp_settings.player_height;  // this is HALF the height in reality
						height = abs(head_projection.Y - center_projection.Y);
					}

					projections_of_predictions.push_back(projection);
					aimbot_information.push_back({(prediction - game_data::my_player.location_).Magnitude(), projection, height});
				}

				*/
			}
		}

		players_previous = game_data::game_data.players;
	}

}  // namespace aimbot

namespace radar {
	/* static */ struct RadarSettings {
		bool enabled = true;
		int radar_poll_frequency = 60 * 5;
		bool show_friendlies = false;
		bool show_flags = true;
	} radar_settings;

	/* static */ Timer get_radar_data_timer(radar_settings.radar_poll_frequency);

	struct RadarLocation {  // polar coordinates
		float r = 0;
		float theta = 0;
		bool right = 0;

		/*
		void Clear(void) {
			r = 0;
			theta = 0;
			right = 0;
		}
		*/
	};

	struct RadarInformation : RadarLocation {
		bool is_friendly = false;
	};

	/* static */ vector<RadarInformation> player_locations;
	/* static */ vector<RadarInformation> flag_locations;

	void Tick(void) {
		if (!radar_settings.enabled || !get_radar_data_timer.IsReady())
			return;

		player_locations.clear();
		flag_locations.clear();

		if (!game_data::my_player.is_valid_)
			;  // return;

		for (vector<game_data::information::Player>::iterator player = game_data::game_data.players.begin(); player != game_data::game_data.players.end(); player++) {
			if (!player->is_valid_) {
				continue;
			}

			game_data::information::Player* p = (game_data::information::Player*)&*player;
			bool same_team = game_data::my_player.IsSameTeam(p);
			if (same_team && !radar_settings.show_friendlies) {
				continue;
			}

			FVector difference_vector = player->location_ - game_data::my_player.location_;
			float angle = math::AngleBetweenVector(game_data::my_player.forward_vector_, difference_vector);
			float magnitude = difference_vector.Magnitude();
			bool right = math::IsVectorToRight(game_data::my_player.forward_vector_, difference_vector);
			RadarInformation radar_information = { magnitude, angle, right, same_team };

			float delta = magnitude * imgui::visuals::radar_visual_settings.zoom_ * imgui::visuals::radar_visual_settings.zoom;
			if (delta > imgui::visuals::radar_visual_settings.window_size / 2 - imgui::visuals::radar_visual_settings.border) {
				continue;
			}

			player_locations.push_back(radar_information);
		}
	}

}  // namespace radar

namespace imgui {
	namespace imgui_menu {
		enum LeftMenuButtons { kAimAssist, kESP, kRadar, kOther, kInformation };
		/* static */ const char* button_text[] = { "Aim assist", "ESP", "Radar", "Other", "Information" };
		/* static */ const int buttons_num = sizeof(button_text) / sizeof(char*);
		/* static */ int selected_index = LeftMenuButtons::kInformation;  // LeftMenuButtons::kAimAssist;
		/* static */ float item_width = -150;

		void DrawInformationMenuNew(void) {
			static ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable | (ImGuiTableFlags_ContextMenuInBody & 0) | (ImGuiTableFlags_NoBordersInBody & 0) | ImGuiTableFlags_BordersOuter;
			if (ImGui::BeginTable("descensiontable", 1, flags, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
				ImGui::TableSetupColumn("descension", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::PushItemWidth(item_width);
				ImGui::Indent();
				const char* info0 =
					"descension ported to tribes 2 v1.0 (Public)\n"
					"Released: 15/03/2023\n";
				//"Game version: -";

				const char* info1 =
					"https://github.com/MuhanjalaRE\n"
					"https://twitter.com/Muhanjala\n"
					"https://dll.rip";
				ImGui::Text(info0);
				ImGui::Separator();
				ImGui::Text(info1);

				ImGui::Unindent();
				ImGui::EndTable();
			}
		}

		void DrawAimAssistMenuNew(void) {
			static ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable | (ImGuiTableFlags_ContextMenuInBody & 0) | (ImGuiTableFlags_NoBordersInBody & 0) | ImGuiTableFlags_BordersOuter;
			if (ImGui::BeginTable("aimassisttable", 1, flags, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
				ImGui::TableSetupColumn("Aim assist", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::PushItemWidth(item_width);
				if (ImGui::CollapsingHeader("General settings")) {
					ImGui::Indent();
					ImGui::Checkbox("Enable aim assist", &aimbot::aimbot_settings.enabled);
					ImGui::Combo("Mode##aim_assist_mode_combo", (int*)&aimbot::aimbot_settings.aimbot_mode, aimbot::mode_labels, IM_ARRAYSIZE(aimbot::mode_labels));

					if (aimbot::aimbot_settings.aimbot_mode == aimbot::AimbotMode::kClosestXhair) {
						ImGui::SliderFloat("Horizontal FOV", &aimbot::aimbot_settings.aimbot_horizontal_fov_angle, 1, 89);
						aimbot::aimbot_settings.aimbot_horizontal_fov_angle_cos = cos(aimbot::aimbot_settings.aimbot_horizontal_fov_angle * PI / 180.0);
						aimbot::aimbot_settings.aimbot_horizontal_fov_angle_cos_sqr = pow(aimbot::aimbot_settings.aimbot_horizontal_fov_angle_cos, 2);
					}

					if (ImGui::SliderInt("Poll rate (Hz)", &aimbot::aimbot_settings.aimbot_poll_frequency, 1, 300)) {
						aimbot::aimbot_poll_timer.SetFrequency(aimbot::aimbot_settings.aimbot_poll_frequency);
					}

					// ImGui::Checkbox("Factor target acceleration (Chaingun only)", &aimbot::aimbot_settings.use_acceleration);
					ImGui::Checkbox("Factor target acceleration", &aimbot::aimbot_settings.use_acceleration);
					if (aimbot::aimbot_settings.use_acceleration) {
						ImGui::Indent();
						ImGui::Checkbox("Factor for Chaingun only", &aimbot::aimbot_settings.use_acceleration_cg_only);
						ImGui::Unindent();
					}

					// ImGui::Text("Factor target accel")
					if (aimbot::aimbot_settings.use_acceleration) {
						// ImGui::SliderFloat("Acceleration delta (ms)", &aimbot::aimbot_settings.acceleration_delta_in_ms, 1, 1000);
					}

					ImGui::Unindent();
				}

				if (ImGui::CollapsingHeader("Target settings")) {
					ImGui::Indent();
					ImGui::Checkbox("Friendly fire", &aimbot::aimbot_settings.friendly_fire);
					ImGui::Checkbox("Need line of sight", &aimbot::aimbot_settings.need_line_of_sight);
					ImGui::Checkbox("Target everyone", &aimbot::aimbot_settings.target_everyone);
					if (!aimbot::aimbot_settings.target_everyone) {
						ImGui::Checkbox("Stay locked on to target", &aimbot::aimbot_settings.stay_locked_to_target);
						ImGui::Checkbox("Auto lock to new target", &aimbot::aimbot_settings.auto_lock_to_new_target);
					}
					ImGui::Unindent();
				}

				if (ImGui::CollapsingHeader("Weapon settings")) {
					ImGui::Indent();
					if (ImGui::CollapsingHeader("Pings")) {
						ImGui::Indent();
						ImGui::SliderFloat("Disc ping", &aimbot::aimbot_settings.disc_ping_in_ms, -300, 300);
						ImGui::SliderFloat("Chaingun ping", &aimbot::aimbot_settings.chaingun_ping_in_ms, -300, 300);
						ImGui::SliderFloat("Grenade Launcher ping", &aimbot::aimbot_settings.grenadelauncher_ping_in_ms, -300, 300);
						ImGui::SliderFloat("Plasma Gun ping", &aimbot::aimbot_settings.plasmagun_ping_in_ms, -300, 300);
						ImGui::SliderFloat("Blaster ping", &aimbot::aimbot_settings.blaster_ping_in_ms, -300, 300);
						ImGui::Unindent();
					}

					if (ImGui::CollapsingHeader("Bullet speeds")) {
						ImGui::Indent();
						ImGui::SliderFloat("Disc speed", &game_data::information::weapon_speeds.disc.bullet_speed, 0, 200);
						ImGui::SliderFloat("Chaingun speed", &game_data::information::weapon_speeds.chaingun.bullet_speed, 0, 900);
						ImGui::SliderFloat("Grenadelauncher speed", &game_data::information::weapon_speeds.grenadelauncher.bullet_speed, 0, 0);
						ImGui::SliderFloat("Plasma speed", &game_data::information::weapon_speeds.plasma.bullet_speed, 0, 110);
						ImGui::SliderFloat("Blaster speed", &game_data::information::weapon_speeds.blaster.bullet_speed, 0, 400);
						ImGui::Unindent();
					}

					if (ImGui::CollapsingHeader("Inheritence")) {
						ImGui::Indent();
						ImGui::SliderFloat("Disc inheritence", &game_data::information::weapon_speeds.disc.inheritence, 0, 1);
						ImGui::SliderFloat("Chaingun inheritence", &game_data::information::weapon_speeds.chaingun.inheritence, 0, 1);
						ImGui::SliderFloat("Grenadelauncher inheritence", &game_data::information::weapon_speeds.grenadelauncher.inheritence, 0, 1);
						ImGui::SliderFloat("Plasma inheritence", &game_data::information::weapon_speeds.plasma.inheritence, 0, 1);
						ImGui::SliderFloat("Blaster inheritence", &game_data::information::weapon_speeds.blaster.inheritence, 0, 1);
						ImGui::Unindent();
					}

					ImGui::Unindent();
				}

				if (ImGui::CollapsingHeader("Markers")) {
					float marker_preview_size = 100;
					ImGui::Combo("Style##aim_assist_combo", (int*)&visuals::aimbot_visual_settings.marker_style, visuals::marker_labels, IM_ARRAYSIZE(visuals::marker_labels));
					ImGui::ColorEdit4("Colour", &visuals::aimbot_visual_settings.marker_colour.Value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_None | ImGuiColorEditFlags_AlphaBar);

					if (visuals::aimbot_visual_settings.marker_style == imgui::visuals::MarkerStyle::kBounds || visuals::aimbot_visual_settings.marker_style == imgui::visuals::MarkerStyle::kFilledBounds) {
						if (visuals::aimbot_visual_settings.marker_style == imgui::visuals::MarkerStyle::kBounds) {
							ImGui::SliderInt("Thickness", &visuals::aimbot_visual_settings.marker_thickness, 1, 10);
						}
					}
					else {
						ImGui::SliderInt("Radius", &visuals::aimbot_visual_settings.marker_size, 1, 10);

						if (visuals::aimbot_visual_settings.marker_style == visuals::MarkerStyle::kCircle || visuals::aimbot_visual_settings.marker_style == visuals::MarkerStyle::kSquare) {
							ImGui::SliderInt("Thickness", &visuals::aimbot_visual_settings.marker_thickness, 1, 10);
						}

						ImGui::Text("Marker preview");

						ImVec2 window_position = ImGui::GetWindowPos();
						ImVec2 window_size = ImGui::GetWindowSize();

						ImDrawList* imgui_draw_list = ImGui::GetWindowDrawList();
						ImVec2 current_cursor_pos = ImGui::GetCursorPos();
						ImVec2 local_cursor_pos = { window_position.x + ImGui::GetCursorPosX(), window_position.y + ImGui::GetCursorPosY() - ImGui::GetScrollY() };
						imgui_draw_list->AddRectFilled(local_cursor_pos, { local_cursor_pos.x + marker_preview_size, local_cursor_pos.y + marker_preview_size }, ImColor(0, 0, 0, 255), 0, 0);
						ImVec2 center = { local_cursor_pos.x + marker_preview_size / 2, local_cursor_pos.y + marker_preview_size / 2 };

						float box_size_height = 40;
						float box_size_width = box_size_height * esp::esp_settings.width_to_height_ratio;

						imgui::visuals::DrawMarker((imgui::visuals::MarkerStyle)visuals::aimbot_visual_settings.marker_style, center, visuals::aimbot_visual_settings.marker_colour, visuals::aimbot_visual_settings.marker_size, visuals::aimbot_visual_settings.marker_thickness);

						current_cursor_pos.y += marker_preview_size;
						ImGui::SetCursorPos(current_cursor_pos);

						ImGui::Spacing();
						ImGui::Checkbox("Scale by distance", &visuals::aimbot_visual_settings.scale_by_distance);
						if (visuals::aimbot_visual_settings.scale_by_distance) {
							ImGui::SliderInt("Distance for scaling", &visuals::aimbot_visual_settings.distance_for_scaling, 1, 15000);
							ImGui::SliderInt("Minimum marker size", &visuals::aimbot_visual_settings.minimum_marker_size, 1, 10);
						}
					}
				}

				if (ImGui::CollapsingHeader("Triggerbot settings")) {
					ImGui::Indent();
					ImGui::Checkbox("Enable triggerbot", &aimbot::aimbot_settings.triggerbot_enabled);
					ImGui::Text("Triggerbot does not work for the Chaingun as it is a hold to fire weapon.");
					ImGui::Unindent();
				}

				ImGui::EndTable();
			}
		}

		void DrawRadarMenuNew(void) {
			static ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable | (ImGuiTableFlags_ContextMenuInBody & 0) | (ImGuiTableFlags_NoBordersInBody & 0) | ImGuiTableFlags_BordersOuter;
			if (ImGui::BeginTable("radartable", 1, flags, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
				ImGui::TableSetupColumn("Radar", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::PushItemWidth(item_width);
				if (ImGui::CollapsingHeader("General settings")) {
					ImGui::Indent();
					ImGui::Checkbox("Enabled##radar_enabled", &radar::radar_settings.enabled);
					if (ImGui::SliderInt("Poll rate (Hz)##radar", &radar::radar_settings.radar_poll_frequency, 1, 300)) {
						radar::get_radar_data_timer.SetFrequency(radar::radar_settings.radar_poll_frequency);
					}

					ImGui::SliderFloat("Zoom", &visuals::radar_visual_settings.zoom, 0, 10);
					ImGui::Checkbox("Show friendlies", &radar::radar_settings.show_friendlies);
					ImGui::Checkbox("Show flags", &radar::radar_settings.show_flags);
					ImGui::Unindent();
				}

				if (ImGui::CollapsingHeader("Visual settings")) {
					ImGui::Indent();
					ImGui::ColorEdit4("Backgrond Colour", &visuals::radar_visual_settings.window_background_colour.Value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_None | ImGuiColorEditFlags_AlphaBar);
					ImGui::Checkbox("Draw axes", &visuals::radar_visual_settings.draw_axes);
					if (visuals::radar_visual_settings.draw_axes)
						ImGui::SliderInt("Axes thickness", &visuals::radar_visual_settings.axes_thickness, 1, 4);
					ImGui::Unindent();
				}

				if (ImGui::CollapsingHeader("Markers")) {
					ImGui::Indent();
					float marker_preview_size = 100;

					ImGui::Combo("Style##radar_combo", (int*)&visuals::radar_visual_settings.marker_style, visuals::marker_labels, IM_ARRAYSIZE(visuals::marker_labels) - 2);
					ImGui::SliderInt("Radius", &visuals::radar_visual_settings.marker_size, 1, 50);

					if (visuals::radar_visual_settings.marker_style == visuals::MarkerStyle::kCircle || visuals::radar_visual_settings.marker_style == visuals::MarkerStyle::kSquare) {
						ImGui::SliderInt("Thickness", &visuals::radar_visual_settings.marker_thickness, 1, 10);
					}

					ImGui::Text("Marker preview");

					ImVec2 window_position = ImGui::GetWindowPos();
					ImVec2 window_size = ImGui::GetWindowSize();

					ImDrawList* imgui_draw_list = ImGui::GetWindowDrawList();
					ImVec2 current_cursor_pos = ImGui::GetCursorPos();
					ImVec2 local_cursor_pos = { window_position.x + ImGui::GetCursorPosX(), window_position.y + ImGui::GetCursorPosY() - ImGui::GetScrollY() };
					imgui_draw_list->AddRectFilled(local_cursor_pos, { local_cursor_pos.x + marker_preview_size, local_cursor_pos.y + marker_preview_size }, ImColor(0, 0, 0, 255), 0, 0);
					ImVec2 center = { local_cursor_pos.x + marker_preview_size / 2, local_cursor_pos.y + marker_preview_size / 2 };

					imgui::visuals::DrawMarker((imgui::visuals::MarkerStyle)visuals::radar_visual_settings.marker_style, center, visuals::radar_visual_settings.enemy_marker_colour, visuals::radar_visual_settings.marker_size, visuals::radar_visual_settings.marker_thickness);

					current_cursor_pos.y += marker_preview_size;
					ImGui::SetCursorPos(current_cursor_pos);
					ImGui::Spacing();

					ImGui::ColorEdit4("Friendly player Colour", &visuals::radar_visual_settings.friendly_marker_colour.Value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_None | ImGuiColorEditFlags_AlphaBar);
					ImGui::ColorEdit4("Enemy player Colour", &visuals::radar_visual_settings.enemy_marker_colour.Value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_None | ImGuiColorEditFlags_AlphaBar);
					ImGui::ColorEdit4("Friendly flag Colour", &visuals::radar_visual_settings.friendly_flag_marker_colour.Value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_None | ImGuiColorEditFlags_AlphaBar);
					ImGui::ColorEdit4("Enemy flag Colour", &visuals::radar_visual_settings.enemy_flag_marker_colour.Value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_None | ImGuiColorEditFlags_AlphaBar);
					ImGui::Unindent();
				}

				ImGui::EndTable();
			}
		}

		void DrawESPMenuNew(void) {
			static ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable | (ImGuiTableFlags_ContextMenuInBody & 0) | (ImGuiTableFlags_NoBordersInBody & 0) | ImGuiTableFlags_BordersOuter;
			if (ImGui::BeginTable("esptable", 1, flags, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
				ImGui::TableSetupColumn("ESP", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::PushItemWidth(item_width);
				if (ImGui::CollapsingHeader("General settings")) {
					ImGui::Indent();
					ImGui::Checkbox("Enabled", &esp::esp_settings.enabled);
					if (ImGui::SliderInt("Poll rate (Hz)", &esp::esp_settings.poll_frequency, 1, 300)) {
						esp::get_esp_data_timer.SetFrequency(esp::esp_settings.poll_frequency);
					}
					ImGui::Checkbox("Show friendlies", &esp::esp_settings.show_friendlies);
					ImGui::Unindent();
				}

				if (ImGui::CollapsingHeader("BoundingBox settings")) {
					ImGui::Indent();
					ImGui::SliderInt("Box thickness", &visuals::esp_visual_settings.bounding_box_settings.box_thickness, 1, 20);
					ImGui::ColorEdit4("Friendly Colour##box", &visuals::esp_visual_settings.bounding_box_settings.friendly_player_box_colour.Value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_None | ImGuiColorEditFlags_AlphaBar);
					ImGui::ColorEdit4("Enemy Colour##box", &visuals::esp_visual_settings.bounding_box_settings.enemy_player_box_colour.Value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_None | ImGuiColorEditFlags_AlphaBar);
					ImGui::Unindent();
				}

				if (ImGui::CollapsingHeader("Snapline settings")) {
					ImGui::Indent();
					ImGui::Checkbox("Show snap lines", &esp::esp_settings.show_lines);
					ImGui::ColorEdit4("Enemy Colour##line", &visuals::esp_visual_settings.line_settings.enemy_player_line_colour.Value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_None | ImGuiColorEditFlags_AlphaBar);
					ImGui::SliderInt("Line thickness", &visuals::esp_visual_settings.line_settings.line_thickness, 1, 20);
					ImGui::Unindent();
				}

				/*
				if (ImGui::CollapsingHeader("Player name settings")) {
					ImGui::Indent();
					ImGui::Checkbox("Show player names", &esp::esp_settings.show_names);
					// ImGui::ColorEdit4("Friendly Colour##name", &visuals::esp_visual_settings.line_settings.enemy_player_line_colour.Value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_None | ImGuiColorEditFlags_AlphaBar);
					ImGui::ColorEdit4("Enemy Colour##name", &visuals::esp_visual_settings.name_settings.enemy_name_colour.Value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_None | ImGuiColorEditFlags_AlphaBar);
					ImGui::Unindent();
				}
				*/

				ImGui::EndTable();
			}
		}

		void DrawOtherMenuNew(void) {
			static ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable | (ImGuiTableFlags_ContextMenuInBody & 0) | (ImGuiTableFlags_NoBordersInBody & 0) | ImGuiTableFlags_BordersOuter;
			if (ImGui::BeginTable("othertable", 1, flags, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
				ImGui::TableSetupColumn("Other", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::PushItemWidth(item_width);
				if (ImGui::CollapsingHeader("Crosshair settings")) {
					ImGui::Indent();

					if (ImGui::CollapsingHeader("General settings##crosshair")) {
						float marker_preview_size = 100;
						ImGui::Indent();
						ImGui::Checkbox("Enabled##crosshair_enabled", &visuals::crosshair_settings.enabled);
						ImGui::Combo("Style##crosshair_combo", (int*)&visuals::crosshair_settings.marker_style, visuals::marker_labels, IM_ARRAYSIZE(visuals::marker_labels) - 2);
						ImGui::ColorEdit4("Colour", &visuals::crosshair_settings.marker_colour.Value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_None | ImGuiColorEditFlags_AlphaBar);
						ImGui::SliderInt("Radius", &visuals::crosshair_settings.marker_size, 1, 10);

						if (visuals::crosshair_settings.marker_style == visuals::MarkerStyle::kCircle || visuals::crosshair_settings.marker_style == visuals::MarkerStyle::kSquare) {
							ImGui::SliderInt("Thickness", &visuals::crosshair_settings.marker_thickness, 1, 10);
						}

						ImGui::Text("Crosshair preview");

						ImVec2 window_position = ImGui::GetWindowPos();
						ImVec2 window_size = ImGui::GetWindowSize();

						ImDrawList* imgui_draw_list = ImGui::GetWindowDrawList();
						ImVec2 current_cursor_pos = ImGui::GetCursorPos();
						ImVec2 local_cursor_pos = { window_position.x + ImGui::GetCursorPosX(), window_position.y + ImGui::GetCursorPosY() };
						imgui_draw_list->AddRectFilled(local_cursor_pos, { local_cursor_pos.x + 100, local_cursor_pos.y + 100 }, ImColor(0, 0, 0, 255), 0, 0);
						ImVec2 center = { local_cursor_pos.x + 100 / 2, local_cursor_pos.y + 100 / 2 };

						imgui::visuals::DrawMarker((imgui::visuals::MarkerStyle)visuals::crosshair_settings.marker_style, center, visuals::crosshair_settings.marker_colour, visuals::crosshair_settings.marker_size, visuals::crosshair_settings.marker_thickness);

						current_cursor_pos.y += marker_preview_size;
						ImGui::SetCursorPos(current_cursor_pos);
						ImGui::Spacing();

						ImGui::Unindent();
					}

					ImGui::Unindent();
				}

				/*
				if (ImGui::CollapsingHeader("Other settings")) {
					ImGui::Indent();
					//ImGui::SliderFloat("FOV", &game_functions::fovv, 0, 120);
					// ImGui::Checkbox("Disable hitmarkers", &other::other_settings.disable_hitmarker);

					ImGui::Unindent();
				}
				*/

				/*
				if (ImGui::CollapsingHeader("Other options")) {
					ImGui::Indent();
					//
					ImGui::Unindent();
				}
				*/

#ifdef USE_AIMTRACKER
				if (ImGui::CollapsingHeader("Aim tracker")) {
					ImGui::Indent();

					ImGui::Checkbox("Enabled", &aimtracker::aimtracker_settings.enabled);

#ifdef SHOW_AIMTRACKER_SETTINGS
					static float f_lifetime = aimtracker::aim_tracker_tick_manager.GetLifeTime();
					static int i_maxcount = aimtracker::aim_tracker_tick_manager.GetMaxCount();
					static int i_ticks_per_second = aimtracker::aim_tracker_tick_manager.GetTicksPerSecond();

					static float f_zoom_pitch = aimtracker::aim_tracker_tick_manager.GetZoomPitch();
					static float f_zoom_yaw = aimtracker::aim_tracker_tick_manager.GetZoomYaw();

					static float f_tool_window_size = aimtracker::aimtracker_settings.window_size.x;

					// ImGui::PushItemWidth(100);

					if (ImGui::SliderInt("Max items", &i_maxcount, 1, 10000)) {
						aimtracker::aim_tracker_tick_manager.SetMaxCount(i_maxcount);
					}

					if (ImGui::SliderFloat("Lifetime", &f_lifetime, 0, 60)) {
						aimtracker::aim_tracker_tick_manager.SetLifeTime(f_lifetime);
					}

					if (ImGui::SliderInt("Ticks per second", &i_ticks_per_second, 1, 300)) {
						aimtracker::aim_tracker_tick_manager.SetTicksPerSecond(i_ticks_per_second);
					}

					if (ImGui::SliderFloat("Pitch zoom", &f_zoom_pitch, 1, 5)) {
						aimtracker::aim_tracker_tick_manager.SetZoomPitch(f_zoom_pitch);
					}

					if (ImGui::SliderFloat("Yaw zoom", &f_zoom_yaw, 1, 5)) {
						aimtracker::aim_tracker_tick_manager.SetZoomYaw(f_zoom_yaw);
					}
					/*
					if (ImGui::SliderFloat("Window size", &f_tool_window_size, 200, 1200)) {
						aimtracker::aimtracker_settings.window_size = {f_tool_window_size, f_tool_window_size};
					}
					*/
#endif

					// ImGui::Unindent();
				}
#endif

				ImGui::EndTable();
			}
		}
	}  // namespace imgui_menu

}  // namespace imgui

namespace game_data {

	void GetWeapon(void) {
		return;
	}

	void GetGameData(void) {
		game_data.Reset();
		GetPlayers();
		GetWeapon();
	}
}  // namespace game_data


namespace hooks {
	void FpsUpdateHook(void) {
		DWORD dwWaitResult = WaitForSingleObject(game_mutex, INFINITE);
		if (true) {
			static bool got_resolution = false;

			//game_data::GetGameData();

			aimbot::Tick();
			radar::Tick();
			esp::Tick();

			// Just get screen resolution every frame, who cares
			if (!got_resolution || true) {
				int x, y;
				ImVec2 display_size = ImGui::GetIO().DisplaySize;
				// cout << display_size.x << ", " << display_size.y << endl;
				x = display_size.x;
				y = display_size.y;

				// game_data::local_player_controller->GetViewportSize(&x, &y);
				game_data::screen_size = { x * 1.0f, y * 1.0f };
				game_data::screen_center = { x * 0.5f, y * 0.5f };
				// got_resolution = true;
			}
		}
		ReleaseMutex(game_mutex);
		game_data::game_data.Reset();
		OriginalFpsUpdate();
	}

	BOOL __stdcall wglSwapBuffersHook(int* arg1) {
		ImGui_ImplOpenGL2_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		DWORD dwWaitResult = WaitForSingleObject(game_mutex, INFINITE);

		DrawImGui();

		ReleaseMutex(game_mutex);

		ImGui::EndFrame();
		ImGui::Render();
		ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

		return OriginalwglSwapBuffers(arg1);
	}

	LRESULT __stdcall SetWindowLongPtrHook(HWND hWnd, int arg1, long arg2) {
		if (arg1 == GWL_WNDPROC) {
			ImGui_ImplWin32_Shutdown();
			ImGui_ImplWin32_Init(hWnd);
		}

		if (arg1 != GWL_WNDPROC) {
			return OriginalSetWindowLongPtr(hWnd, arg1, arg2);
		}
		else {
			original_windowproc_callback = (WNDPROC)OriginalSetWindowLongPtr(hWnd, arg1, arg2);
			OriginalSetWindowLongPtr(hWnd, GWL_WNDPROC, (LONG_PTR)CustomWindowProcCallback);
			return (LRESULT)original_windowproc_callback;
		}
	}

	void __fastcall SetImageHook(void* this_shapebase, void* _, unsigned int imageSlot, void* imageData, void* skinNameHandle, bool loaded, bool ammo, bool triggerDown, bool target) {
		OriginalSetImage(this_shapebase, imageSlot, imageData, skinNameHandle, loaded, ammo, triggerDown, target);
		if (imageData && imageSlot == 0 && this_shapebase == game_data::my_player.character_) {
			void* namespace_ = GET_OBJECT_VARIABLE_BY_OFFSET(void*, imageData, 36);
			if (namespace_) {

				char* namespace_name_ = GET_OBJECT_VARIABLE_BY_OFFSET(char*, namespace_, 0);
				char* shape_file = GET_OBJECT_VARIABLE_BY_OFFSET(char*, imageData, 3212);

				if (namespace_name_ && shape_file) {
					std::string weapon_string(shape_file);
					if (weapon_string == "weapon_chaingun.dts") {
						game_data::my_player.weapon_ = game_data::Weapon::cg;
						game_data::my_player.weapon_type_ = game_data::WeaponType::kProjectileLinear;
					}
					else if (weapon_string == "weapon_disc.dts") {
						game_data::my_player.weapon_ = game_data::Weapon::disc;
						game_data::my_player.weapon_type_ = game_data::WeaponType::kProjectileLinear;
					}
					else if (weapon_string == "weapon_grenade_launcher.dts") {
						game_data::my_player.weapon_ = game_data::Weapon::gl;
						game_data::my_player.weapon_type_ = game_data::WeaponType::kProjectileArching;
					}
					else if (weapon_string == "weapon_plasma.dts") {
						game_data::my_player.weapon_ = game_data::Weapon::plasma;
						game_data::my_player.weapon_type_ = game_data::WeaponType::kProjectileLinear;
					}
					else if (weapon_string == "weapon_sniper.dts") {
						game_data::my_player.weapon_ = game_data::Weapon::sniper;
						game_data::my_player.weapon_type_ = game_data::WeaponType::kHitscan;
					}
					else if (weapon_string == "weapon_shocklance.dts") {
						game_data::my_player.weapon_ = game_data::Weapon::shocklance;
						game_data::my_player.weapon_type_ = game_data::WeaponType::kHitscan;
					}
					else if (weapon_string == "weapon_energy.dts") {
						game_data::my_player.weapon_ = game_data::Weapon::blaster;
						game_data::my_player.weapon_type_ = game_data::WeaponType::kProjectileLinear;
					}
					else {
						game_data::my_player.weapon_ = game_data::Weapon::none;
						game_data::my_player.weapon_type_ = game_data::WeaponType::kHitscan;
					}
				}
			}
		}
	}

	void __fastcall SetRenderPositionHook(void* this_player, void* _, void* arg1, void* arg2, void* arg3) {

		static game_data::information::Player player_information;
		player_information.Setup(this_player);

		if (player_information.is_valid_) {
			if (!GET_OBJECT_VARIABLE_BY_OFFSET(void*, this_player, 181 * 4)) {
				game_data::game_data.players.push_back(player_information);  // game_data.players does not include ourselves
			}
			else {
				game_data::my_player.Setup(this_player);
			}
		}

		OriginalPlayerSetRenderPosition(this_player, arg1, arg2, arg3);
	}

	bool __fastcall CastRayHook(void* this_container, void* _, FVector& a2, FVector& a3, unsigned int a4, RayInfo* a5) {
		container = this_container;
		return OriginalCastRay(this_container, a2, a3, a4, a5);
	}

	int __stdcall GluProjectHook(double objx, double objy, double objz, const double modelMatrix[16], const double projMatrix[16], const int viewport[4], double* winx, double* winy, double* winz) {
		memcpy(model_matrix, modelMatrix, sizeof(double) * 16);
		memcpy(proj_matrix, projMatrix, sizeof(double) * 16);
		memcpy(viewport_, viewport, sizeof(int) * 4);
		int res = OriginalGluProject(objx, objy, objz, modelMatrix, projMatrix, viewport, winx, winy, winz);
		return res;
	}
}


void OnDLLProcessAttach(void) {

#ifdef _DEBUG
	AllocConsole();
	freopen("CONOUT$", "w", stdout);
	static plog::ConsoleAppender<plog::TxtFormatter> consoleAppender;
	plog::init(plog::verbose, &consoleAppender);
	PLOG_DEBUG << "DLL injected successfully. Hooking game functions.";
#endif

	HWND hwnd = FindWindowA(NULL, "Tribes 2");
	original_windowproc_callback = (WNDPROC)SetWindowLongPtr(hwnd, GWL_WNDPROC, (LONG_PTR)CustomWindowProcCallback);

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	HMODULE hModule = GetModuleHandle(L"User32.dll");
	unsigned int setwindowlongptr_address = (unsigned int)GetProcAddress(hModule, "SetWindowLongW");
	hooks::OriginalSetWindowLongPtr = (hooks::SetWindowLongPtr_)setwindowlongptr_address;
	DetourAttach(&(PVOID&)hooks::OriginalSetWindowLongPtr, hooks::SetWindowLongPtrHook);

	hModule = GetModuleHandle(L"opengl32.dll");
	unsigned int wglswapbuffers_address = (unsigned int)GetProcAddress(hModule, "wglSwapBuffers");
	hooks::OriginalwglSwapBuffers = (hooks::wglSwapBuffers)wglswapbuffers_address;
	DetourAttach(&(PVOID&)hooks::OriginalwglSwapBuffers, hooks::wglSwapBuffersHook);

	hModule = GetModuleHandle(L"glu32.dll");
	unsigned int gluproject_address = (unsigned int)GetProcAddress(hModule, "gluProject");
	hooks::OriginalGluProject = (hooks::GluProject)gluproject_address;
	DetourAttach(&(PVOID&)hooks::OriginalGluProject, hooks::GluProjectHook);

	DetourAttach(&(PVOID&)hooks::OriginalFpsUpdate, hooks::FpsUpdateHook);
	DetourAttach(&(PVOID&)hooks::OriginalPlayerSetRenderPosition, hooks::SetRenderPositionHook);
	DetourAttach(&(PVOID&)hooks::OriginalCastRay, hooks::CastRayHook);
	DetourAttach(&(PVOID&)hooks::OriginalSetImage, hooks::SetImageHook);

	DetourTransactionCommit();

	game_mutex = CreateMutex(NULL, false, NULL);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplOpenGL2_Init();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)OnDLLProcessAttach, NULL, NULL, NULL);
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

LRESULT WINAPI CustomWindowProcCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	bool* window_locked = (bool*)0x0083BFE5;
	static bool previous_window_locked_state = *window_locked;
	static bool menu_state_changed = false;

	if (msg == WM_KEYDOWN) {
		if (wParam == VK_INSERT) {
			imgui_show_menu = !imgui_show_menu;
			if (imgui_show_menu) {
				previous_window_locked_state = *window_locked;
			}
			menu_state_changed = true;
		}
		if (wParam == VK_CONTROL) {
			aimbot::Reset();
		}
		if (wParam == 16) {
			aimbot::aimbot_settings.enabled_aimbot = true;
		}
	}
	else if (msg == WM_KEYUP) {
		if (wParam == 16) {
			aimbot::aimbot_settings.enabled_aimbot = false;
		}
	}

	ImGuiIO& io = ImGui::GetIO();
	if (imgui_show_menu) {
		io.MouseDrawCursor = true;

		if (*window_locked && menu_state_changed) {
			hooks::OriginalSetWindowLocked(false);
			*window_locked = false;
		}

		ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
		return true;
	}
	else {
		io.MouseDrawCursor = false;

		if (*window_locked != previous_window_locked_state && menu_state_changed) {
			hooks::OriginalSetWindowLocked(previous_window_locked_state);
			*window_locked = previous_window_locked_state;
		}
	}
	return CallWindowProc(original_windowproc_callback, hWnd, msg, wParam, lParam);
}

void DrawImGui(void) {

	using namespace imgui;

	if (aimbot::aimbot_settings.enabled) {
		ImGui::SetNextWindowPos({ 0, 0 });
		ImGui::SetNextWindowSize({ game_data::screen_size.X, game_data::screen_size.Y });
		ImGui::Begin("aim_assist", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);

		int marker_size = visuals::aimbot_visual_settings.marker_size;

		vector<aimbot::AimbotInformation>& aimbot_informations = aimbot::aimbot_information;

		for (vector<aimbot::AimbotInformation>::iterator i = aimbot_informations.begin(); i != aimbot_informations.end(); i++) {
			ImVec2 v(i->projection_.X, i->projection_.Y);

			if (visuals::aimbot_visual_settings.scale_by_distance) {
				marker_size = (visuals::aimbot_visual_settings.marker_size - visuals::aimbot_visual_settings.minimum_marker_size) * exp(-i->distance_ / visuals::aimbot_visual_settings.distance_for_scaling) + visuals::aimbot_visual_settings.minimum_marker_size;
			}

			float box_size_height = i->height;
			float box_size_width = box_size_height * esp::esp_settings.width_to_height_ratio;

			if (visuals::aimbot_visual_settings.marker_style == imgui::visuals::MarkerStyle::kBounds) {
				ImDrawList* imgui_draw_list = ImGui::GetWindowDrawList();
				imgui_draw_list->AddRect({ v.x - box_size_width, v.y - box_size_height }, { v.x + box_size_width, v.y + box_size_height }, visuals::aimbot_visual_settings.marker_colour, 0, 0, visuals::aimbot_visual_settings.marker_thickness);
			}
			else if (visuals::aimbot_visual_settings.marker_style == imgui::visuals::MarkerStyle::kFilledBounds) {
				ImDrawList* imgui_draw_list = ImGui::GetWindowDrawList();
				imgui_draw_list->AddRectFilled({ v.x - box_size_width, v.y - box_size_height }, { v.x + box_size_width, v.y + box_size_height }, visuals::aimbot_visual_settings.marker_colour, 0);
			}
			else {
				imgui::visuals::DrawMarker((imgui::visuals::MarkerStyle)visuals::aimbot_visual_settings.marker_style, v, visuals::aimbot_visual_settings.marker_colour, marker_size, visuals::aimbot_visual_settings.marker_thickness);
			}
		}

		ImGui::End();
	}

	if (esp::esp_settings.enabled) {
		ImGui::SetNextWindowPos({ 0, 0 });
		ImGui::SetNextWindowSize({ game_data::screen_size.X, game_data::screen_size.Y });
		ImGui::Begin("esp", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
		ImDrawList* imgui_draw_list = ImGui::GetWindowDrawList();

		ImColor friendly_box_colour = visuals::esp_visual_settings.bounding_box_settings.friendly_player_box_colour;
		ImColor enemy_box_colour = visuals::esp_visual_settings.bounding_box_settings.enemy_player_box_colour;
		int box_thickness = visuals::esp_visual_settings.bounding_box_settings.box_thickness;

		/* static */ ImColor colour = enemy_box_colour;
		for (vector<esp::ESPInformation>::iterator esp_information = esp::esp_information.begin(); esp_information != esp::esp_information.end(); esp_information++) {
			if (esp_information->projection.X <= 0 && esp_information->projection.Y <= 0)
				continue;

			colour = (esp_information->is_friendly) ? friendly_box_colour : enemy_box_colour;
			ImVec2 projection(esp_information->projection.X, esp_information->projection.Y);
			float box_size_height = esp_information->height;
			float box_size_width = box_size_height * esp::esp_settings.width_to_height_ratio;

			imgui_draw_list->AddRect({ projection.x - box_size_width, projection.y - box_size_height }, { projection.x + box_size_width, projection.y + box_size_height }, colour, 0, 0, box_thickness);

			if (esp::esp_settings.show_lines && !esp_information->is_friendly) {
				imgui_draw_list->AddLine({ game_data::screen_size.X / 2, game_data::screen_size.Y }, { projection.x, projection.y + box_size_height }, visuals::esp_visual_settings.line_settings.enemy_player_line_colour, visuals::esp_visual_settings.line_settings.line_thickness);
			}

			if (esp::esp_settings.show_names && !esp_information->is_friendly) {
				ImColor colour = visuals::esp_visual_settings.name_settings.enemy_name_colour;
				colour = (esp_information->is_friendly) ? visuals::esp_visual_settings.name_settings.friendly_name_colour : visuals::esp_visual_settings.name_settings.enemy_name_colour;
				ImGui::GetFont()->Scale = visuals::esp_visual_settings.name_settings.scale;
				ImGui::PushFont(ImGui::GetFont());
				ImVec2 text_size = ImGui::CalcTextSize(esp_information->name.c_str());
				imgui_draw_list->AddText({ projection.x - text_size.x / 2, projection.y - box_size_height - text_size.y - visuals::esp_visual_settings.name_settings.name_height_offset }, colour, esp_information->name.c_str());
				ImGui::GetFont()->Scale = 1;
				ImGui::PopFont();
			}
		}

		ImGui::End();
	}

	if (visuals::crosshair_settings.enabled) {
		/* static */ ImVec2 window_size(30, 30);

		ImVec2 display_size = ImGui::GetIO().DisplaySize;

		ImGui::SetNextWindowPos({ display_size.x / 2 - window_size.x / 2, display_size.y / 2 - window_size.y / 2 });
		ImGui::SetNextWindowSize(window_size);
		ImGui::Begin("crosshair", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		imgui::visuals::DrawMarker((imgui::visuals::MarkerStyle)visuals::crosshair_settings.marker_style, { display_size.x / 2, display_size.y / 2 }, visuals::crosshair_settings.marker_colour, visuals::crosshair_settings.marker_size, visuals::crosshair_settings.marker_thickness);

		ImGui::End();
	}

	if (radar::radar_settings.enabled) {
		ImGui::SetNextWindowPos(visuals::radar_visual_settings.window_location, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize({ (float)visuals::radar_visual_settings.window_size, (float)visuals::radar_visual_settings.window_size });

		if (imgui_show_menu) {
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImColor(0.0f, 0.0f, 0.0f, 0.0f).Value);
			ImGui::Begin("Radar##radar", NULL, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse /*| ImGuiWindowFlags_NoBringToFrontOnFocus*/);
		}
		else {
			ImGui::Begin("Radar##radar", NULL, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse /*| ImGuiWindowFlags_NoBringToFrontOnFocus*/);
		}

		ImVec2 window_position = ImGui::GetWindowPos();
		ImVec2 window_size = ImGui::GetWindowSize();
		ImVec2 center(window_position.x + window_size.x / 2, window_position.y + window_size.y / 2);

		visuals::radar_visual_settings.window_size = (window_size.x >= window_size.y) ? window_size.x : window_size.y;
		float border = visuals::radar_visual_settings.border;

		visuals::radar_visual_settings.window_location = window_position;
		int axes_thickness = visuals::radar_visual_settings.axes_thickness;

		ImDrawList* imgui_draw_list = ImGui::GetWindowDrawList();
		imgui_draw_list->AddCircleFilled(center, window_size.x / 2 - border, visuals::radar_visual_settings.window_background_colour, 0);
		imgui_draw_list->AddCircle(center, window_size.x / 2 - border, visuals::radar_visual_settings.window_background_colour, 0, axes_thickness);

		if (visuals::radar_visual_settings.draw_axes) {
			imgui_draw_list->AddLine({ window_position.x + border, window_position.y + window_size.y / 2 }, { window_position.x + window_size.x - border, window_position.y + window_size.y / 2 }, ImColor(65, 65, 65, 255), axes_thickness);
			imgui_draw_list->AddLine({ window_position.x + window_size.x / 2, window_position.y + border }, { window_position.x + window_size.x / 2, window_position.y + window_size.y - border }, ImColor(65, 65, 65, 255), axes_thickness);
			imgui_draw_list->AddCircleFilled(center, axes_thickness + 1, ImColor(65, 65, 65, 125));
		}

		ImColor friendly_marker_colour = visuals::radar_visual_settings.friendly_marker_colour;
		ImColor enemy_marker_colour = visuals::radar_visual_settings.enemy_marker_colour;
		/* static */ ImColor player_marker_colour = enemy_marker_colour;
		for (vector<radar::RadarInformation>::iterator radar_information = radar::player_locations.begin(); radar_information != radar::player_locations.end(); radar_information++) {
			float theta = radar_information->theta;
			float y = radar_information->r * cos(theta) * visuals::radar_visual_settings.zoom_ * visuals::radar_visual_settings.zoom;
			float x = radar_information->r * sin(theta) * visuals::radar_visual_settings.zoom_ * visuals::radar_visual_settings.zoom;

			if (!radar_information->right)
				x = -abs(x);

			player_marker_colour = (radar_information->is_friendly) ? friendly_marker_colour : enemy_marker_colour;

			imgui::visuals::DrawMarker((imgui::visuals::MarkerStyle)visuals::radar_visual_settings.marker_style, { center.x + x, center.y - y }, player_marker_colour, visuals::radar_visual_settings.marker_size, visuals::radar_visual_settings.marker_thickness);
		}

		if (radar::radar_settings.show_flags) {
			ImColor friendly_flag_marker_colour = visuals::radar_visual_settings.friendly_flag_marker_colour;
			ImColor enemy_flag_marker_colour = visuals::radar_visual_settings.enemy_flag_marker_colour;

			for (vector<radar::RadarInformation>::iterator radar_information = radar::flag_locations.begin(); radar_information != radar::flag_locations.end(); radar_information++) {
				float theta = radar_information->theta;
				float y = radar_information->r * cos(theta) * visuals::radar_visual_settings.zoom_ * visuals::radar_visual_settings.zoom;
				float x = radar_information->r * sin(theta) * visuals::radar_visual_settings.zoom_ * visuals::radar_visual_settings.zoom;

				if (!radar_information->right)
					x = -abs(x);

				ImColor flag_colour = (radar_information->is_friendly) ? friendly_flag_marker_colour : enemy_flag_marker_colour;

				imgui::visuals::DrawMarker((imgui::visuals::MarkerStyle)visuals::radar_visual_settings.marker_style, { center.x + x, center.y - y }, flag_colour, visuals::radar_visual_settings.marker_size, visuals::radar_visual_settings.marker_thickness);
			}
		}

		if (imgui_show_menu) {
			ImGui::PopStyleColor();
		}
		ImGui::End();
	}

	if (imgui_show_menu) {
		// static bool unused_boolean = true;

		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4* colors = style.Colors;

		// ImGui::SetNextWindowPos({300, 300}, ImGuiCond_FirstUseEver);

		ImVec2 window_size_(800, 500);

		ImGui::SetNextWindowSize(window_size_, ImGuiCond_FirstUseEver);
		ImVec2 display_size = ImGui::GetIO().DisplaySize;
		ImGui::SetNextWindowPos({ display_size.x / 2 - window_size_.x / 2, display_size.y / 2 - window_size_.y / 2 }, ImGuiCond_FirstUseEver);

		/* static */ ImVec2 padding = ImGui::GetStyle().FramePadding;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding.x, 8));
		ImGui::Begin("descension ported to tribes 2 v1.0", NULL, ImGuiWindowFlags_AlwaysAutoResize & 0);
		ImGui::PopStyleVar();

		ImVec2 window_position = ImGui::GetWindowPos();
		ImVec2 window_size = ImGui::GetWindowSize();
		ImVec2 center(window_position.x + window_size.x / 2, window_position.y + window_size.y / 2);

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar & 0;

		/* static */ float left_menu_width = 125;
		/* static */ float child_height_offset = 10;

		ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_ChildBg, ImColor(0, 0, 0, 0).Value);
		ImGui::BeginChild("MenuL", ImVec2(left_menu_width, ImGui::GetContentRegionAvail().y - child_height_offset), false, window_flags);

		for (int i = 0; i < imgui_menu::buttons_num; i++) {
			ImVec2 size = ImVec2(left_menu_width * 0.75, 0);
			bool b_selected = i == imgui_menu::selected_index;
			if (b_selected) {
				size.x = left_menu_width * 0.95;
				ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_::ImGuiCol_ButtonHovered]);
				ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(1, 0));
			}

			if (*imgui_menu::button_text[i] != '-') {
				if (ImGui::Button(imgui_menu::button_text[i], size)) {
					imgui_menu::selected_index = i;
				}
			}

			if (b_selected) {
				ImGui::PopStyleColor();
				ImGui::PopStyleVar();
			}
		}

		ImGui::EndChild();
		ImGui::SameLine();

		ImGui::BeginChild("MenuR", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y - child_height_offset), false, window_flags);
		ImGui::PopStyleColor();

		switch (imgui_menu::selected_index) {
		case imgui_menu::LeftMenuButtons::kInformation:
			imgui_menu::DrawInformationMenuNew();
			break;
		case imgui_menu::LeftMenuButtons::kAimAssist:
			imgui_menu::DrawAimAssistMenuNew();
			break;
		case imgui_menu::LeftMenuButtons::kRadar:
			imgui_menu::DrawRadarMenuNew();
			break;
		case imgui_menu::LeftMenuButtons::kESP:
			imgui_menu::DrawESPMenuNew();
			break;
		case imgui_menu::LeftMenuButtons::kOther:
			imgui_menu::DrawOtherMenuNew();
			break;
		}

		ImGui::EndChild();
		ImGui::End();
	}

}  // namespace ue