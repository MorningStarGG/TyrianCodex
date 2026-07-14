#pragma once
#include <cmath>
#include <algorithm>

// Minimal vector/matrix math to project GW2 world positions onto the ImGui canvas.
// GW2 / MumbleLink is right-handed, Y up, metres. Camera position + front come from
// MumbleLink; vertical FOV (radians) from the Mumble identity. (Convention matches the
// proven Pathing reference addon so projection lines up with the game.)
namespace Math
{
    struct Vec3
    {
        float x = 0, y = 0, z = 0;
        Vec3() = default;
        Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

        Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
        Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
        Vec3 operator*(float s)       const { return { x * s, y * s, z * s }; }
        float Dot(const Vec3& o)      const { return x * o.x + y * o.y + z * o.z; }
        Vec3 Cross(const Vec3& o)     const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
        float LengthSq() const { return x * x + y * y + z * z; }
        float Length()   const { return std::sqrt(LengthSq()); }
        Vec3 Normalised() const { float l = Length(); return l > 1e-8f ? Vec3{ x / l, y / l, z / l } : Vec3{}; }
    };

    // 4x4 column-major: m[col][row].
    struct Mat4
    {
        float m[4][4] = {};

        void Transform(float ix, float iy, float iz, float& ox, float& oy, float& oz, float& ow) const
        {
            ox = m[0][0] * ix + m[1][0] * iy + m[2][0] * iz + m[3][0];
            oy = m[0][1] * ix + m[1][1] * iy + m[2][1] * iz + m[3][1];
            oz = m[0][2] * ix + m[1][2] * iy + m[2][2] * iz + m[3][2];
            ow = m[0][3] * ix + m[1][3] * iy + m[2][3] * iz + m[3][3];
        }

        Mat4 operator*(const Mat4& b) const
        {
            Mat4 r{};
            for (int c = 0; c < 4; ++c)
                for (int row = 0; row < 4; ++row)
                    for (int k = 0; k < 4; ++k)
                        r.m[c][row] += m[k][row] * b.m[c][k];
            return r;
        }
    };

    // Combined view*projection from the MumbleLink camera. This is GW2's LEFT-HANDED
    // convention (right = up x front, view third row +f, projection w_clip = +z_view),
    // ported verbatim from the Pathing addon's BuildViewProj - the version that actually
    // sticks to the ground. (Its MathUtils LookAt/Perspective are stale/right-handed and
    // were NOT what its renderer used.) Uses CameraTop as the up vector, not a fixed (0,1,0).
    inline Mat4 BuildViewProj(Vec3 camPos, Vec3 camFront, Vec3 camTop, float fovY,
                              float screenW, float screenH, float nearZ = 0.5f, float farZ = 8000.f)
    {
        Vec3 f = camFront.Normalised();
        Vec3 worldUp = (camTop.LengthSq() > 0.01f) ? camTop.Normalised() : Vec3{ 0.f, 1.f, 0.f };
        Vec3 r = worldUp.Cross(f).Normalised();
        Vec3 u = f.Cross(r).Normalised();

        Mat4 view{};
        view.m[0][0] = r.x; view.m[1][0] = r.y; view.m[2][0] = r.z; view.m[3][0] = -r.Dot(camPos);
        view.m[0][1] = u.x; view.m[1][1] = u.y; view.m[2][1] = u.z; view.m[3][1] = -u.Dot(camPos);
        view.m[0][2] = f.x; view.m[1][2] = f.y; view.m[2][2] = f.z; view.m[3][2] = -f.Dot(camPos);
        view.m[3][3] = 1.f;

        float aspect = (screenH > 0.f) ? screenW / screenH : 1.7778f;
        float t = std::tan(fovY * 0.5f);

        Mat4 proj{};
        proj.m[0][0] = 1.f / (aspect * t);
        proj.m[1][1] = 1.f / t;
        proj.m[2][2] = farZ / (farZ - nearZ);
        proj.m[2][3] = 1.f;                                  // w_clip = +z_view (left-handed)
        proj.m[3][2] = -(nearZ * farZ) / (farZ - nearZ);

        return proj * view;
    }

    // World -> screen (top-left origin, pixels). False if behind camera or well off-screen.
    inline bool WorldToScreen(const Vec3& world, const Mat4& viewProj,
                              float screenW, float screenH, float& sx, float& sy, float& depth)
    {
        float cx, cy, cz, cw;
        viewProj.Transform(world.x, world.y, world.z, cx, cy, cz, cw);
        if (cw <= 0.f) return false;              // behind camera
        float ndcX = cx / cw, ndcY = cy / cw;
        if (ndcX < -1.3f || ndcX > 1.3f || ndcY < -1.3f || ndcY > 1.3f) return false;
        sx = (ndcX + 1.f) * 0.5f * screenW;
        sy = (-ndcY + 1.f) * 0.5f * screenH;
        depth = cz / cw;
        return true;
    }

    inline float Remap(float v, float lo, float hi, float outLo, float outHi)
    {
        if (hi <= lo) return outLo;
        return outLo + (outHi - outLo) * std::clamp((v - lo) / (hi - lo), 0.f, 1.f);
    }
}
