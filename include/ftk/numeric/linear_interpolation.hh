#ifndef _FTK_LINEAR_INTERPOLATION_H
#define _FTK_LINEAR_INTERPOLATION_H

namespace ftk {

template <typename T>
inline T linear_interpolation_1simplex(const T v[2], const T mu[2])
{
  return v[0] * mu[0] + v[1] * mu[1];
}

template <typename T>
inline void linear_interpolation_1simplex_vector2(const T V[2][2], const T mu[2], T v[2])
{
  v[0] = V[0][0] * mu[0] + V[1][0] * mu[1];
  v[1] = V[0][1] * mu[0] + V[1][1] * mu[1];
}

template <typename T>
inline T linear_interpolation_2simplex(const T v[3], const T mu[3])
{
  return v[0] * mu[0] + v[1] * mu[1] + v[2] * mu[2];
}

template <typename T>
inline void linear_interpolation_2simplex_vector2(const T V[3][2], const T mu[3], T v[2])
{
  v[0] = V[0][0] * mu[0] + V[1][0] * mu[1] + V[2][0] * mu[2];
  v[1] = V[0][1] * mu[0] + V[1][1] * mu[1] + V[2][1] * mu[2];
}

template <typename T>
inline void linear_interpolation_2simplex_vector3(const T V[3][3], const T mu[3], T v[3])
{
  v[0] = V[0][0] * mu[0] + V[1][0] * mu[1] + V[2][0] * mu[2];
  v[1] = V[0][1] * mu[0] + V[1][1] * mu[1] + V[2][1] * mu[2];
  v[2] = V[0][2] * mu[0] + V[1][2] * mu[1] + V[2][2] * mu[2];
}

template <typename T>
inline void linear_interpolation_2simplex_matrix2x2(const T V[3][2][2], const T mu[3], T v[2][2])
{
  v[0][0] = V[0][0][0] * mu[0] + V[1][0][0] * mu[1] + V[2][0][0] * mu[2];
  v[0][1] = V[0][0][1] * mu[0] + V[1][0][1] * mu[1] + V[2][0][1] * mu[2];
  v[1][0] = V[0][1][0] * mu[0] + V[1][1][0] * mu[1] + V[2][1][0] * mu[2];
  v[1][1] = V[0][1][1] * mu[0] + V[1][1][1] * mu[1] + V[2][1][1] * mu[2];
}

template <typename T>
inline T linear_interpolation_3simplex(const T v[4], const T mu[4])
{
  return v[0] * mu[0] + v[1] * mu[1] + v[2] * mu[2] + v[3] * mu[3];
}

template <typename T>
inline void linear_interpolation_3simplex_vector3(const T V[4][3], const T mu[4], T v[3])
{
  v[0] = V[0][0] * mu[0] + V[1][0] * mu[1] + V[2][0] * mu[2] + V[3][0] * mu[3];
  v[1] = V[0][1] * mu[0] + V[1][1] * mu[1] + V[2][1] * mu[2] + V[3][1] * mu[3];
  v[2] = V[0][2] * mu[0] + V[1][2] * mu[1] + V[2][2] * mu[2] + V[3][2] * mu[3];
}

template <typename T>
inline void linear_interpolation_3simplex_vector4(const T V[4][4], const T mu[4], T v[4])
{
  v[0] = V[0][0] * mu[0] + V[1][0] * mu[1] + V[2][0] * mu[2] + V[3][0] * mu[3];
  v[1] = V[0][1] * mu[0] + V[1][1] * mu[1] + V[2][1] * mu[2] + V[3][1] * mu[3];
  v[2] = V[0][2] * mu[0] + V[1][2] * mu[1] + V[2][2] * mu[2] + V[3][2] * mu[3];
  v[3] = V[0][3] * mu[0] + V[1][3] * mu[1] + V[2][3] * mu[2] + V[3][3] * mu[3];
}

template <typename T>
inline void linear_interpolation_3simplex_matrix3x3(const T V[4][3][3], const T mu[4], T v[3][3])
{
  for (int j = 0; j < 3; j ++)
    for (int k = 0; k < 3; k ++) {
      v[j][k] = T(0);
      for (int i = 0; i < 4; i ++) 
        v[j][k] += V[i][j][k] * mu[i];
    }
}

}

#endif
