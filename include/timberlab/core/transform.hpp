#pragma once

#include "timberlab/core/component.hpp"

#include <cmath>

namespace timberlab {

// Column-major 4x4. World = T * Rz * Ry * Rx * local.
// Rotations are degrees about the local axes, applied Rx then Ry then Rz.
inline void setIdentity(double m[16]) {
  for (int i = 0; i < 16; ++i) m[i] = 0;
  m[0] = m[5] = m[10] = m[15] = 1;
}

inline void multiplyColumnMajor(const double a[16], const double b[16], double out[16]) {
  double r[16];
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      r[col * 4 + row] = a[row] * b[col * 4] + a[4 + row] * b[col * 4 + 1] +
                         a[8 + row] * b[col * 4 + 2] + a[12 + row] * b[col * 4 + 3];
    }
  }
  for (int i = 0; i < 16; ++i) out[i] = r[i];
}

inline void transformPoint(const double m[16], double x, double y, double z, double& ox,
                           double& oy, double& oz) {
  ox = m[0] * x + m[4] * y + m[8] * z + m[12];
  oy = m[1] * x + m[5] * y + m[9] * z + m[13];
  oz = m[2] * x + m[6] * y + m[10] * z + m[14];
}

inline void placementMatrix(const Placement& placement, double out[16]) {
  const double rx = placement.rxDeg * 0.017453292519943295;
  const double ry = placement.ryDeg * 0.017453292519943295;
  const double rz = placement.rzDeg * 0.017453292519943295;
  const double cx = std::cos(rx);
  const double sx = std::sin(rx);
  const double cy = std::cos(ry);
  const double sy = std::sin(ry);
  const double cz = std::cos(rz);
  const double sz = std::sin(rz);

  double rotX[16];
  double rotY[16];
  double rotZ[16];
  double trans[16];
  setIdentity(rotX);
  setIdentity(rotY);
  setIdentity(rotZ);
  setIdentity(trans);

  rotX[5] = cx;
  rotX[6] = sx;
  rotX[9] = -sx;
  rotX[10] = cx;

  rotY[0] = cy;
  rotY[2] = -sy;
  rotY[8] = sy;
  rotY[10] = cy;

  rotZ[0] = cz;
  rotZ[1] = sz;
  rotZ[4] = -sz;
  rotZ[5] = cz;

  trans[12] = static_cast<double>(placement.xNm) / 1'000'000.0;
  trans[13] = static_cast<double>(placement.yNm) / 1'000'000.0;
  trans[14] = static_cast<double>(placement.zNm) / 1'000'000.0;

  double ryRx[16];
  double rzRyRx[16];
  multiplyColumnMajor(rotY, rotX, ryRx);
  multiplyColumnMajor(rotZ, ryRx, rzRyRx);
  multiplyColumnMajor(trans, rzRyRx, out);
}

}  // namespace timberlab
