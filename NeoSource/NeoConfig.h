#pragma once

#include <cstring>
#include <cmath>
#include <cstdint>
#include <limits>

// 이전 .ns 회귀 전용 빌드만 범용 3항 산술을 포함한다. NeoCore 기본 빌드에는 넣지 않는다.

namespace NeoScript
{


typedef unsigned char	u8;
typedef char			s8;
typedef unsigned short	u16;
typedef short			s16;
typedef unsigned int	u32;
typedef int				s32;

// 일반 실수와 실수 배열은 double, 벡터 성분은 별도의 float 저장소를 쓴다.
typedef double		NS_FLOAT;
// 스크립트 정수는 64비트이며 명령어·슬롯·컬렉션 개수는 32비트를 유지한다.
using NS_INT = int64_t;
using NS_UINT = uint64_t;
static constexpr NS_INT NS_INT_MIN = (std::numeric_limits<NS_INT>::min)();
static constexpr NS_INT NS_INT_MAX = (std::numeric_limits<NS_INT>::max)();

// MOVF는 32비트 IEEE float 비트를 담고, 실행 시 일반 double 값으로 확장한다.
static_assert(sizeof(float) == 4 && sizeof(int) == 4 && std::numeric_limits<float>::is_iec559,
    "MOVF requires 32-bit IEEE float and 32-bit int");

#pragma pack(1)
struct debug_info
{
	union
	{
		u32			_data;
		struct
		{
			u16		_fileseq;
			u16		_lineseq;
		};
	};

	debug_info()
	{
		_fileseq = 0;
		_lineseq = 0;
	}
	debug_info(u16 file, u16 line)
	{
		_fileseq = file;
		_lineseq = line;
	}
};
#pragma pack()



#define FILE_NEOS	(('N' << 24) | ('E' << 16) | ('O' << 8) | ('S'))
// 0110: switch/case 테이블 chunk 추가 (이전 캐시 이미지와 호환 안 됨)
// 0111: _L(전 오퍼랜드 로컬) opcode 를 각 원본 뒤에 삽입 → 이후 op 번호가 전부 밀림.
//       이미지 자체는 원본 op 만 담고 _L 치환은 로드 후(PatchLocalOps) 이뤄지지만,
//       0110 이미지를 그대로 읽으면 op 번호가 어긋나므로 버전을 올린다.
// 0112: 벡터 타입 통합 — VAR_VEC2/3/4/QUAT -> VAR_VEC(+성분수), VEC*_MAKE 4개 -> VEC_MAKE.
//       VAR_TYPE / opcode 번호가 모두 밀리므로 이전 이미지와 호환되지 않는다.
// 0113: NOP_MOVI_L 뒤에 NOP_MOVF/_L 삽입. 이후 opcode 번호가 바뀌므로 캐시를 재생성한다.
// 0114: 인라인 문자 타입을 제거하고 foreach 문자열 원소도 VAR_STRING 으로 통합.
//       VAR_TYPE 값이 바뀌므로 이전 캐시 이미지는 읽을 수 없다.
// 0115: 함수 테이블에 익명 함수의 지역 BoundaryValue 캡처 슬롯을 추가.
// 0116: 다단계 익명 함수 캡처가 중간 부모 프레임의 숨은 슬롯을 경유하도록 컴파일된다.
//       0115 이미지에는 그 중간 슬롯이 없으므로 캐시를 다시 만들어야 한다.
// 0118: RET_CLOSURE의 _L 변형을 제거한다. 0117 캐시는 서로 다른 opcode 번호를 쓰므로
//       다시 만들어야 한다.
// 0119: RET_CLOSURE opcode를 제거하고 closure 반환 여부를 SCallStack에 기록한다.
//       이후 opcode 번호가 바뀌므로 캐시를 다시 만들어야 한다.
// 0120: 범위 조건 jump opcode와 'RNGE' 범위 descriptor chunk를 추가한다.
//       이전 캐시는 새 opcode/chunk를 모르므로 다시 만들어야 한다.
// 0121: 원시 배열(VAR_ARRAY)을 추가한다. 이전 캐시는 enum 번호가 달라 다시 만든다.
// 0122: signed global slots and LOADK32 beside MOV; rebuild older compiled images.
// 0123: remove unused collection removal opcodes; renumbering requires rebuilding cached images.
// 0124: 사슬 lvalue 되써 넣기 opcode 추가. 이전 캐시는 이 op 를 모르므로 다시 만든다.
// 0125: internal VAR_REF and atomic chained writes; rebuild older images.
// 0126: preserve owning slots for selectors after indexed assignments; regenerate cached code.
// 0127: fold chained writeback into two-word WREF; opcode renumbering requires new images.
// 0128: WREF tails are ordinary collection writes; regenerate cached images.
// 0129: ordered jumps preserve logical negation for NaN; regenerate cached images.
// 0130: checked shift counts and defined folding; rebuild images with previously folded shifts.
// 0131: retain every foreach source on entry; regenerate previously compiled loops.
// 0132: preserve signed-zero constants; rebuild images with previously merged zeros.
// 0133: initialize locals and enforce ordered operands and deferred assignment paths.
// 0134: 정수 값과 직렬화된 정수 상수를 64비트로 확장한다.
// 0135: 반환 직전 전역·캡처 대입을 보존하므로 기존 이미지를 다시 컴파일한다.
// 0136: 실수 상수를 double로 저장하므로 이전 이미지는 다시 컴파일한다.
// 0137: double 값을 정확히 보존하는 float32 즉값에 MOVF를 사용한다. 구 로더는 이를 거부한다.
// 0142: 복합 대입 융합 명령(READ_ADD/SUB/MUL/DIV, dest op= clt[key]). 0138~0141 실험 이미지는 거부한다.
// 0143: 2·3성분 벡터를 인라인 VAR_VEC로 분리하여 타입 번호가 바뀐다. 이전 이미지는 다시 만든다.
// 0144: 정적 숫자 산술·변환 명령을 추가한다. 이전 이미지는 다시 컴파일한다.
// 0145: 정적 숫자 비교와 조건 분기 명령을 추가한다. 이전 이미지는 다시 컴파일한다.
// 0146: 정적 벡터·배열·셀 명령과 정수 비트 연산 명령을 추가한다.
// 0147: 생성되지 않는 비트 연산·타입 검증 명령 3개를 제거한다. 이전 캐시는 다시 만든다.
// 0148: 생성되지 않는 컨테이너 복합 대입 명령 5개를 제거한다. 이전 캐시는 다시 만든다.
// 0149/L149: 정적/이전 동적 명령 집합을 구분하여 서로의 이미지를 로드하지 않는다.
// 0150/L150: 압축 int32/float32 배열의 생성·읽기·쓰기·추가 전용 명령을 추가한다.
// 0151: 이전 동적 산술 명령 집합과 생성되지 않는 논리·증감·타입·대입결과 명령 10개를 제거한다. 이전 캐시는 다시 만든다.
// 0152: 맵의 엄격한 읽기와 존재 확인 명령을 추가한다. 이전 캐시는 다시 만든다.
// 0153: 집합의 정수·문자열 멤버십·삽입·제거 전용 명령을 추가한다. 이전 캐시는 다시 만든다.
// 0156: 슬롯 위치 비트를 이동하고 숫자 명령을 계열과 세부 연산으로 나눈다. 이전 캐시는 다시 만든다.
// 0157: 정수 루프 준비 명령을 포함한 융합 임시값 수명 검증을 갱신한다. 이전 캐시는 다시 만든다.
// 0159: 컴파일러가 함수·전역 슬롯과 프레임 정리 계획을 저장한다. 이전 캐시는 다시 만든다.
// 0160: 벡터 성분 수 필드를 제거하고 3성분 저장 타입을 분리한다. 이전 캐시는 다시 만든다.
// 0161: 8바이트 무태그 슬롯과 함수 반환·호출 창의 고정 타입 배치를 저장한다. 이전 캐시는 다시 만든다.
// 0162: 소유 슬롯을 인자 뒤에 연속 배치하고 슬롯별 복사·해제 정책을 기록한다. 이전 캐시는 다시 만든다.
// 0163: 퓨전(FUSED/EXTEND, FUSE 청크)을 제거해 opcode 번호가 바뀐다. 이전 캐시는 다시 만든다.
// 0164: getter 접기가 파서로 옮겨가 창 반환 슬롯에 쓰는 복사가 없다. 로더가 그 형태를 더 받지 않으므로 이전 캐시는 다시 만든다.
// 0165: 반환 명령에 결과 정책과 무소유 프레임 표시를 넣는다. 이전 캐시는 다시 만든다.
// 0166: 스칼라 컨테이너 누적 읽기(LOAD_ADD) 명령이 추가되어 opcode 번호가 바뀐다. 이전 캐시는 다시 만든다.
// 0167: 순수 수학 네이티브 호출 앞에 PREPARE_CALL을 내지 않는다. 이전 런타임은 그 호출에서 계획을 읽으므로 이전 캐시는 다시 만든다.
// 0168: 내장 API 호출이 연결 이름(소유자.이름)을 들고 로드 때 본체 번호로 연결된다. 메서드형 API 명령(APICALL)이 생기고
//       벡터 생성·순수 수학 직접 계산 명령이 없어져 opcode 번호가 바뀐다. 이전 캐시는 다시 만든다.
#define NEO_VER (('0' << 24) | ('1' << 16) | ('6' << 8) | ('8'))

#if defined(_MSC_VER) && !defined(_DEBUG)
#define NEOS_FORCEINLINE __forceinline
#elif defined(__GNUC__) && __GNUC__ >= 4 && defined(NDEBUG)
// always_inline 만으로는 inline 이 아니다. 헤더에 정의가 들어가면 TU 마다 실체가 생겨
// 중복 정의로 링크가 깨지므로 inline 을 반드시 함께 붙인다.
#define NEOS_FORCEINLINE inline __attribute__((always_inline))
#else
#define NEOS_FORCEINLINE inline
#endif

// 콜드 핸들러 out-of-line 강제용. RunInternal 이 L1I(32KB) 를 넘지 않게 덩치 큰/드문 op 를 분리.
#if defined(_MSC_VER)
#define NEOS_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define NEOS_NOINLINE __attribute__((noinline))
#else
#define NEOS_NOINLINE
#endif

};

#include <vector>
#include <list>
#include <string>
#include <map>
#include <unordered_map>
#include <set>
#include <atomic>
