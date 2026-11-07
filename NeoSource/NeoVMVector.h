#pragma once

#include "NeoVMMap.h"

namespace NeoScript
{
// 3·4성분 벡터의 공유 저장소. 2성분 벡터만 값 영역 안에 직접 저장한다.
// Quaternion 은 엔진 컨벤션대로 v[0..3] = w,x,y,z 순서.
//
// refcount 로 공유하되 값 의미론을 유지한다 — 대입은 공유(incref)만 하고, 성분을 쓰는
// 순간(v[i] = x) 공유 중이면 복제한다(copy-on-write). 그래서 `var b = a;` 뒤에 b 를
// 고쳐도 a 는 그대로다.
struct VecInfo : AllocBase
{
	float v[4];
};

NEOS_FORCEINLINE float BoundaryValue::VectorComponent(int index) const
{
	if (GetType() == VAR_VEC) return _vecXY[index];
	return _vec->v[index];
}
template<int Dimension>
NEOS_FORCEINLINE void BoundaryValue::CopyVectorComponents(float* out) const
{
    static_assert(Dimension >= 2 && Dimension <= 4, "vector dimension must be 2..4");
    assert(VectorComponentCount() == Dimension);
    if (Dimension == 2) std::memcpy(out, _vecXY, sizeof(_vecXY));
    else std::memmove(out, _vec->v, sizeof(float) * Dimension);
}
NEOS_FORCEINLINE void BoundaryValue::CopyVectorComponents(float* out) const
{
	// 호출자가 요청한 유효 성분만 쓴다. 남는 출력 레인은 보존한다.
	if (GetType() == VAR_VEC)
		std::memcpy(out, _vecXY, sizeof(_vecXY));
	else
		std::memmove(out, _vec->v, sizeof(float) * VectorComponentCount());
}

inline float ValueRef::VectorComponent(int index) const { return GetType() == VAR_VEC ? value_->_vecXY[index] : value_->_vec->v[index]; }
inline void ValueRef::CopyVectorComponents(float* out) const
{
    if (GetType() == VAR_VEC) std::memcpy(out, value_->_vecXY, sizeof(value_->_vecXY));
    else std::memmove(out, value_->_vec->v, sizeof(float) * VectorComponentCount());
}
template<int Dimension> inline void ValueRef::CopyVectorComponents(float* out) const
{
    assert(VectorComponentCount() == Dimension);
    if (Dimension == 2) std::memcpy(out, value_->_vecXY, sizeof(value_->_vecXY));
    else std::memmove(out, value_->_vec->v, sizeof(float) * Dimension);
}

} // namespace NeoScript
