#define WIN32_LEAN_AND_MEAN
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <intrin.h>
#define Assert(cond) do { if (!(cond)) __debugbreak(); } while (0)

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shobjidl_core.h>
#include <shlguid.h>
#include <commdlg.h>
#include <wincodec.h>
#include <shlwapi.h>

#undef near
#undef far
#define CLAMP(a,min,max) ((a) < (min) ? (min) : (a) > (max) ? (max) : (a))
#define COUNT(arr) (sizeof(arr)/sizeof(*arr))
#define SWAP(temp,a,b) (temp)=(a); (a)=(b); (b)=temp
#define RGBA(r,g,b,a) (r | (g<<8) | (b<<16) | (a<<24))
#define RED(c) (c & 0xff)
#define GREEN(c) ((c>>8) & 0xff)
#define BLUE(c) ((c>>16) & 0xff)
#define ALPHA(c) ((c>>24) & 0xff)
#define REDF(c) (RED(c)/255.0f)
#define BLUEF(c) (BLUE(c)/255.0f)
#define GREENF(c) (GREEN(c)/255.0f)
#define ALPHAF(c) (ALPHA(c)/255.0f)

typedef struct {
	size_t len;
	char *ptr;
} Bytes;

void FatalErrorA(char *format, ...){
	va_list args;
	va_start(args,format);
#if _DEBUG
	vprintf(format,args);
	printf("\n");
#else
	char msg[512];
	vsprintf(msg,format,args);
	MessageBoxA(0,msg,"Error",MB_ICONEXCLAMATION);
#endif
	va_end(args);
	exit(1);
}
void FatalErrorW(WCHAR *format, ...){
	va_list args;
	va_start(args,format);
#if _DEBUG
	vwprintf(format,args);
	wprintf(L"\n");
#else
	WCHAR msg[512];
	vswprintf(msg,COUNT(msg),format,args);
	MessageBoxW(0,msg,L"Error",MB_ICONEXCLAMATION);
#endif
	va_end(args);
	exit(1);
}

void *MallocOrDie(size_t size){
	void *p = malloc(size);
	Assert(p);
	if (!p) FatalErrorA("malloc failed.");
	return p;
}
void *ZallocOrDie(size_t size){
	void *p = calloc(1,size);
	if (!p) FatalErrorA("calloc failed.");
	return p;
}
void *ReallocOrDie(void *ptr, size_t size){
	void *p = realloc(ptr,size);
	if (!p) FatalErrorA("realloc failed.");
	return p;
}

/*
randint
From: https://stackoverflow.com/a/822361
Generates uniform random integers in range [0,n).
*/
int randint(int n){
	if ((n - 1) == RAND_MAX){
		return rand();
	} else {
		// Chop off all of the values that would cause skew...
		int end = RAND_MAX / n; // truncate skew
		end *= n;
		// ... and ignore results from rand() that fall above that limit.
		// (Worst case the loop condition should succeed 50% of the time,
		// so we can expect to bail out of this loop pretty quickly.)
		int r;
		while ((r = rand()) >= end);
		return r % n;
	}
}

void StringToLower(size_t len, char *str){
	for (size_t i = 0; i < len; i++){
		str[i] = tolower(str[i]);
	}
}

#define LIST_INIT(list)\
	(list)->total = 0;\
	(list)->used = 0;\
	(list)->elements = 0;
#define LIST_FREE(list)\
	if ((list)->total){\
		free((list)->elements);\
		(list)->total = 0;\
		(list)->used = 0;\
		(list)->elements = 0;\
	}
#define LIST_IMPLEMENTATION(type)\
typedef struct type##List {\
	size_t total, used;\
	type *elements;\
} type##List;\
type *type##ListMakeRoom(type ## List *list, size_t count){\
	if (list->used+count > list->total){\
		if (!list->total) list->total = 1;\
		while (list->used+count > list->total) list->total *= 2;\
		list->elements = ReallocOrDie(list->elements,list->total*sizeof(type));\
	}\
	return list->elements+list->used;\
}\
void type##ListMakeRoomAtIndex(type ## List *list, size_t index, size_t count){\
	type##ListMakeRoom(list,count);\
	if (index != list->used) memmove(list->elements+index+count,list->elements+index,(list->used-index)*sizeof(type));\
}\
void type##ListInsert(type ## List *list, size_t index, type *elements, size_t count){\
	type##ListMakeRoomAtIndex(list,index,count);\
	memcpy(list->elements+index,elements,count*sizeof(type));\
	list->used += count;\
}\
void type##ListAppend(type ## List *list, type *elements, size_t count){\
	type##ListMakeRoom(list,count);\
	memcpy(list->elements+list->used,elements,count*sizeof(type));\
	list->used += count;\
}

size_t fnv1a(size_t keylen, char *key){
	size_t index = 2166136261u;
	for (size_t i = 0; i < keylen; i++){
		index ^= key[i];
		index *= 16777619;
	}
	return index;
}

#define LINKED_HASHLIST_INIT(list)\
	(list)->total = 0;\
	(list)->used = 0;\
	(list)->buckets = 0;\
	(list)->first = 0;\
	(list)->last = 0;
#define LINKED_HASHLIST_FREE(list)\
	if ((list)->total){\
		free((list)->buckets);\
		LINKED_HASHLIST_INIT(list);\
	}
#define LINKED_HASHLIST_IMPLEMENTATION(type)\
typedef struct type##LinkedHashListBucket {\
	struct type##LinkedHashListBucket *prev, *next;\
	size_t keylen;\
	char *key;\
	type value;\
} type##LinkedHashListBucket;\
typedef struct type##LinkedHashList {\
	size_t total, used;\
	type##LinkedHashListBucket *buckets, *first, *last;\
} type##LinkedHashList;\
type##LinkedHashListBucket *type##LinkedHashListGetBucket(type##LinkedHashList *list, size_t keylen, char *key){\
	if (!list->total) return 0;\
	size_t index = fnv1a(keylen,key);\
	index %= list->total;\
	type##LinkedHashListBucket *lastTombstone = 0;\
	while (1){\
		type##LinkedHashListBucket *bucket = list->buckets+index;\
		if (bucket->keylen < 0) lastTombstone = bucket;\
		else if (!bucket->keylen) return lastTombstone ? lastTombstone : bucket;\
		else if (bucket->keylen == keylen && !memcmp(bucket->key,key,keylen)) return bucket;\
		index = (index + 1) % list->total;\
	}\
}\
type *type##LinkedHashListGet(type##LinkedHashList *list, size_t keylen, char *key){\
	type##LinkedHashListBucket *bucket = type##LinkedHashListGetBucket(list,keylen,key);\
	if (bucket && bucket->keylen > 0) return &bucket->value;\
	else return 0;\
}\
type *type##LinkedHashListNew(type##LinkedHashList *list, size_t keylen, char *key){\
	if (!list->total){\
		list->total = 8;\
		list->buckets = ZallocOrDie(8*sizeof(*list->buckets));\
	}\
	if (list->used+1 > (list->total*3)/4){\
		type##LinkedHashList newList;\
		newList.total = list->total * 2;\
		newList.used = list->used;\
		newList.buckets = ZallocOrDie(newList.total*sizeof(*newList.buckets));\
		for (type##LinkedHashListBucket *bucket = list->first; bucket;){\
			type##LinkedHashListBucket *newBucket = type##LinkedHashListGetBucket(&newList,bucket->keylen,bucket->key);\
			newBucket->keylen = bucket->keylen;\
			newBucket->key = bucket->key;\
			newBucket->value = bucket->value;\
			newBucket->next = 0;\
			if (bucket == list->first){\
				newBucket->prev = 0;\
				newList.first = newBucket;\
				newList.last = newBucket;\
			} else {\
				newBucket->prev = newList.last;\
				newList.last->next = newBucket;\
				newList.last = newBucket;\
			}\
			bucket = bucket->next;\
		}\
		free(list->buckets);\
		*list = newList;\
	}\
	type##LinkedHashListBucket *bucket = type##LinkedHashListGetBucket(list,keylen,key);\
	if (bucket->keylen <= 0){\
		list->used++;\
		bucket->keylen = keylen;\
		bucket->key = key;\
		bucket->next = 0;\
		if (list->last){\
			bucket->prev = list->last;\
			list->last->next = bucket;\
			list->last = bucket;\
		} else {\
			bucket->prev = 0;\
			list->first = bucket;\
			list->last = bucket;\
		}\
	}\
	return &bucket->value;\
}\
void type##LinkedHashListRemove(type##LinkedHashList *list, size_t keylen, char *key){\
	type##LinkedHashListBucket *bucket = type##LinkedHashListGetBucket(list,keylen,key);\
	if (bucket->keylen > 0){\
		if (bucket->prev){\
			if (bucket->next){\
				bucket->prev->next = bucket->next;\
				bucket->next->prev = bucket->prev;\
			} else {\
				bucket->prev->next = 0;\
				list->last = bucket->prev;\
			}\
		} else if (bucket->next){\
			list->first = bucket->next;\
			bucket->next->prev = 0;\
		}\
		bucket->keylen = -1;\
		list->used--;\
	}\
}

/***************************** Linear Algebra */
#define LERP(a,b,t) ((a) + (t)*((b)-(a)))
inline float Float3Dot(float a[3], float b[3]){
	return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
inline float Float3Length(float a[3]){
	return sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
}
inline float Float3AngleBetween(float a[3], float b[3]){
	return acosf((a[0]*b[0] + a[1]*b[1] + a[2]*b[2])/(sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2])*sqrtf(b[0]*b[0] + b[1]*b[1] + b[2]*b[2])));
}
inline void Float3Cross(float a[3], float b[3], float out[3]){
	out[0] = a[1]*b[2] - a[2]*b[1];
	out[1] = a[2]*b[0] - a[0]*b[2];
	out[2] = a[0]*b[1] - a[1]*b[0];
}
inline void Float3Add(float a[3], float b[3]){
	for (int i = 0; i < 3; i++) a[i] += b[i];
}
inline void Float3Subtract(float a[3], float b[3]){
	for (int i = 0; i < 3; i++) a[i] -= b[i];
}
inline void Float3Negate(float a[3]){
	for (int i = 0; i < 3; i++) a[i] = -a[i];
}
inline void Float3Scale(float a[3], float scale){
	for (int i = 0; i < 3; i++) a[i] = a[i]*scale;
}
inline void Float3Normalize(float a[3]){
	float d = 1.0f / sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
	for (int i = 0; i < 3; i++) a[i] *= d;
}
inline void Float3SetLength(float a[3], float length){
	float d = length / sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
	for (int i = 0; i < 3; i++) a[i] *= d;
}
inline void Float3Midpoint(float a[3], float b[3], float out[3]){
	for (int i = 0; i < 3; i++) out[i] = a[i] + 0.5f*(b[i]-a[i]);
}
inline void Float3Lerp(float a[3], float b[3], float t){
	for (int i = 0; i < 3; i++) a[i] = LERP(a[i],b[i],t);
}
inline void ClampEuler(float e[3]){
	for (int i = 0; i < 3; i++){
		if (e[i] > 4.0f*M_PI) e[i] -= 4.0f*M_PI;
		else if (e[i] < -4.0f*M_PI) e[i] += 4.0f*M_PI;
	}
}
inline void QuaternionSlerp(float a[4], float b[4], float t){//from https://github.com/microsoft/referencesource/blob/51cf7850defa8a17d815b4700b67116e3fa283c2/System.Numerics/System/Numerics/Quaternion.cs#L289C8-L289C8
	float cosOmega = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
	bool flip = false;
	if (cosOmega < 0.0f){
		flip = true;
		cosOmega = -cosOmega;
	}
	float s1,s2;
	if (cosOmega > (1.0f - 1e-6f)){
		s1 = 1.0f - t;
		s2 = flip ? -t : t;
	} else {
		float omega = acosf(cosOmega);
		float invSinOmega = 1.0f / sinf(omega);
		s1 = sinf((1.0f-t)*omega)*invSinOmega;
		float sm = sinf(t*omega)*invSinOmega;
		s2 = flip ? -sm : sm;
	}
	for (int i = 0; i < 4; i++) a[i] = s1*a[i] + s2*b[i];
}
inline void Float4x4Identity(float a[16]){
	a[0] = 1.0f;
	a[1] = 0.0f;
	a[2] = 0.0f;
	a[3] = 0.0f;

	a[4] = 0.0f;
	a[5] = 1.0f;
	a[6] = 0.0f;
	a[7] = 0.0f;

	a[8] = 0.0f;
	a[9] = 0.0f;
	a[10] = 1.0f;
	a[11] = 0.0f;

	a[12] = 0.0f;
	a[13] = 0.0f;
	a[14] = 0.0f;
	a[15] = 1.0f;
}
inline void Float4x4FromBasisVectors(float a[16], float x[3], float y[3], float z[3]){
	a[0] = x[0];
	a[1] = x[1];
	a[2] = x[2];
	a[3] = 0.0f;

	a[4] = y[0];
	a[5] = y[1];
	a[6] = y[2];
	a[7] = 0.0f;

	a[8] = z[0];
	a[9] = z[1];
	a[10] = z[2];
	a[11] = 0.0f;

	a[12] = 0.0f;
	a[13] = 0.0f;
	a[14] = 0.0f;
	a[15] = 1.0f;
}
inline void Float4x4Transpose(float a[16]){
	/*
	0 4 8  12
	1 5 9  13
	2 6 10 14
	3 7 11 15
	*/
	float t;
	SWAP(t,a[1],a[4]);
	SWAP(t,a[2],a[8]);
	SWAP(t,a[3],a[12]);
	SWAP(t,a[6],a[9]);
	SWAP(t,a[7],a[13]);
	SWAP(t,a[11],a[14]);
}
inline void Float4x4TransposeMat3(float a[16]){
	/*
	0 4 8  12
	1 5 9  13
	2 6 10 14
	3 7 11 15
	*/
	float t;
	SWAP(t,a[1],a[4]);
	SWAP(t,a[2],a[8]);
	SWAP(t,a[6],a[9]);
}
inline void Float4x4Orthogonal(float a[16], float left, float right, float bottom, float top, float near, float far){
	a[0] = 2.0f/(right-left);
	a[1] = 0.0f;
	a[2] = 0.0f;
	a[3] = 0.0f;

	a[4] = 0.0f;
	a[5] = 2.0f/(top-bottom);
	a[6] = 0.0f;
	a[7] = 0.0f;

	a[8] = 0.0f;
	a[9] = 0.0f;
	a[10] = 2.0f/(near-far);
	a[11] = 0.0f;

	a[12] = (right+left)/(left-right);
	a[13] = (top+bottom)/(bottom-top);
	a[14] = (far+near)/(near-far);
	a[15] = 1.0f;
}
inline void Float4x4Perspective(float a[16], float fovRadians, float aspectRatio, float near, float far){
	float s = 1.0f / tanf(fovRadians * 0.5f);
	float d = near - far;

	a[0] = s/aspectRatio;
	a[1] = 0.0f;
	a[2] = 0.0f;
	a[3] = 0.0f;

	a[4] = 0.0f;
	a[5] = s;
	a[6] = 0.0f;
	a[7] = 0.0f;

	a[8] = 0.0f;
	a[9] = 0.0f;
	a[10] = (far+near)/d;
	a[11] = -1.0f;

	a[12] = 0.0f;
	a[13] = 0.0f;
	a[14] = (2.0f*far*near)/d;
	a[15] = 0.0f;
}
inline void Float4x4Scale(float a[16], float x, float y, float z){
	a[0] = x;
	a[1] = 0.0f;
	a[2] = 0.0f;
	a[3] = 0.0f;

	a[4] = 0.0f;
	a[5] = y;
	a[6] = 0.0f;
	a[7] = 0.0f;

	a[8] = 0.0f;
	a[9] = 0.0f;
	a[10] = z;
	a[11] = 0.0f;

	a[12] = 0.0f;
	a[13] = 0.0f;
	a[14] = 0.0f;
	a[15] = 1.0f;
}
inline void Float4x4ScaleV(float a[16], float scale[3]){
	a[0] = scale[0];
	a[1] = 0.0f;
	a[2] = 0.0f;
	a[3] = 0.0f;

	a[4] = 0.0f;
	a[5] = scale[1];
	a[6] = 0.0f;
	a[7] = 0.0f;

	a[8] = 0.0f;
	a[9] = 0.0f;
	a[10] = scale[2];
	a[11] = 0.0f;

	a[12] = 0.0f;
	a[13] = 0.0f;
	a[14] = 0.0f;
	a[15] = 1.0f;
}
inline void Float4x4Translation(float a[16], float x, float y, float z){
	a[0] = 1.0f;
	a[1] = 0.0f;
	a[2] = 0.0f;
	a[3] = 0.0f;

	a[4] = 0.0f;
	a[5] = 1.0f;
	a[6] = 0.0f;
	a[7] = 0.0f;

	a[8] = 0.0f;
	a[9] = 0.0f;
	a[10] = 1.0f;
	a[11] = 0.0f;

	a[12] = x;
	a[13] = y;
	a[14] = z;
	a[15] = 1.0f;
}
inline void Float4x4TranslationV(float a[16], float translation[3]){
	a[0] = 1.0f;
	a[1] = 0.0f;
	a[2] = 0.0f;
	a[3] = 0.0f;

	a[4] = 0.0f;
	a[5] = 1.0f;
	a[6] = 0.0f;
	a[7] = 0.0f;

	a[8] = 0.0f;
	a[9] = 0.0f;
	a[10] = 1.0f;
	a[11] = 0.0f;

	a[12] = translation[0];
	a[13] = translation[1];
	a[14] = translation[2];
	a[15] = 1.0f;
}
inline void Float4x4RotationX(float a[16], float angle){
	float c = cosf(angle);
	float s = sinf(angle);

	a[0] = 1.0f;
	a[1] = 0.0f;
	a[2] = 0.0f;
	a[3] = 0.0f;

	a[4] = 0.0f;
	a[5] = c;
	a[6] = s;
	a[7] = 0.0f;

	a[8] = 0.0f;
	a[9] = -s;
	a[10] = c;
	a[11] = 0.0f;

	a[12] = 0.0f;
	a[13] = 0.0f;
	a[14] = 0.0f;
	a[15] = 1.0f;
}
inline void Float4x4RotationY(float a[16], float angle){
	float c = cosf(angle);
	float s = sinf(angle);

	a[0] = c;
	a[1] = 0.0f;
	a[2] = -s;
	a[3] = 0.0f;

	a[4] = 0.0f;
	a[5] = 1.0f;
	a[6] = 0.0f;
	a[7] = 0.0f;

	a[8] = s;
	a[9] = 0.0f;
	a[10] = c;
	a[11] = 0.0f;

	a[12] = 0.0f;
	a[13] = 0.0f;
	a[14] = 0.0f;
	a[15] = 1.0f;
}
inline void Float4x4RotationZ(float a[16], float angle){
	float c = cosf(angle);
	float s = sinf(angle);

	a[0] = c;
	a[1] = s;
	a[2] = 0.0f;
	a[3] = 0.0f;

	a[4] = -s;
	a[5] = c;
	a[6] = 0.0f;
	a[7] = 0.0f;

	a[8] = 0.0f;
	a[9] = 0.0f;
	a[10] = 1.0f;
	a[11] = 0.0f;

	a[12] = 0.0f;
	a[13] = 0.0f;
	a[14] = 0.0f;
	a[15] = 1.0f;
}
inline void Float4x4Multiply(float a[16], float b[16], float out[16]){
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			out[i*4+j] = a[j]*b[i*4] + a[j+4]*b[i*4+1] + a[j+8]*b[i*4+2] + a[j+12]*b[i*4+3];
}
inline void Float4x4MultiplyFloat4(float m[16], float v[4], float out[4]){
	for (int i = 0; i < 4; i++) out[i] = v[0]*m[i] + v[1]*m[4+i] + v[2]*m[8+i] + v[3]*m[12+i];
}
inline void Float4x4LookAtY(float m[16], float eye[3], float target[3]){//rotates eye about Y to look at target
	float kx = target[0] - eye[0];
	float kz = target[2] - eye[2];
	float d = 1.0f / sqrtf(kx*kx + kz*kz);
	kx *= d;
	kz *= d;

	m[0] = -kz;
	m[1] = 0.0f;
	m[2] = kx;
	m[3] = 0.0f;

	m[4] = 0.0f;
	m[5] = 1.0f;
	m[6] = 0.0f;
	m[7] = 0.0f;

	m[8] = -kx;
	m[9] = 0.0f;
	m[10] = -kz;
	m[11] = 0.0f;

	m[12] = 0.0f;
	m[13] = 0.0f;
	m[14] = 0.0f;
	m[15] = 1.0f;
}
inline void Float4x4LookAtXY(float m[16], float eye[3], float target[3]){//rotates eye about X and Y to look at target
	float kx = target[0] - eye[0],
		ky = target[1] - eye[1],
		kz = target[2] - eye[2];
	float d = 1.0f / sqrtf(kx*kx + ky*ky + kz*kz);
	kx *= d;
	ky *= d;
	kz *= d;

	float ix = -kz,
		iz = kx;
	d = 1.0f / sqrtf(ix*ix + iz*iz);
	ix *= d;
	iz *= d;

	float jx = -iz*ky,
		jy = iz*kx - ix*kz,
		jz = ix*ky;

	m[0] = ix;
	m[1] = 0.0f;
	m[2] = iz;
	m[3] = 0.0f;

	m[4] = jx;
	m[5] = jy;
	m[6] = jz;
	m[7] = 0.0f;

	m[8] = -kx;
	m[9] = -ky;
	m[10] = -kz;
	m[11] = 0.0f;

	m[12] = 0.0f;
	m[13] = 0.0f;
	m[14] = 0.0f;
	m[15] = 1.0f;
}
inline void Float4x4LookAtXYView(float m[16], float eye[3], float target[3]){//produces a view matrix
	float kx = target[0] - eye[0],
		ky = target[1] - eye[1],
		kz = target[2] - eye[2];
	float d = 1.0f / sqrtf(kx*kx + ky*ky + kz*kz);
	kx *= d;
	ky *= d;
	kz *= d;

	float ix = -kz,
		iz = kx;
	d = 1.0f / sqrtf(ix*ix + iz*iz);
	ix *= d;
	iz *= d;

	float jx = -iz*ky,
		jy = iz*kx - ix*kz,
		jz = ix*ky;

	m[0] = ix;
	m[1] = jx;
	m[2] = -kx;
	m[3] = 0.0f;

	m[4] = 0.0f;
	m[5] = jy;
	m[6] = -ky;
	m[7] = 0.0f;

	m[8] = iz;
	m[9] = jz;
	m[10] = -kz;
	m[11] = 0.0f;

	m[12] = -(ix*eye[0] + iz*eye[2]);
	m[13] = -(jx*eye[0] + jy*eye[1] + jz*eye[2]);
	m[14] = kx*eye[0] + ky*eye[1] + kz*eye[2];
	m[15] = 1.0f;
}
/*
inline void Float4x4LookAtXYViewShake(Float4x4 *m, Float3 *eye, Float3 *target){
Float4x4LookAtXY(m,eye,target);

}
Float4x4 mat4LookAtShake(FVec3 eye, FVec3 target, FVec3 shakeEuler){

FVec3 z = fvec3Norm(fvec3Sub(eye,target)),
x = fvec3Norm(fvec3Cross(z,(FVec3){0,1,0})),
y = fvec3Cross(x,z);
return mat4Mul(mat4Transpose(mat4Mul(eulerToFloat4x4(shakeEuler),mat4Basis(fvec3Negate(x),y,z))),mat4Pos(fvec3Negate(eye)));
}
Float4x4 eulerToFloat4x4(FVec3 e){
return mat4Mul(mat4RotZ(e.z),mat4Mul(mat4RotY(e.y),mat4RotX(e.x)));
}
Float4x4 quatToFloat4x4(FVec4 q){
Float4x4 m = mat4Identity();
m.a0 = 1 - 2*(q.y*q.y + q.z*q.z);
m.a1 = 2*(q.x*q.y + q.z*q.w);
m.a2 = 2*(q.x*q.z - q.y*q.w);

m.b0 = 2*(q.x*q.y - q.z*q.w);
m.b1 = 1 - 2*(q.x*q.x + q.z*q.z);
m.b2 = 2*(q.y*q.z + q.x*q.w);

m.c0 = 2*(q.x*q.z + q.y*q.w);
m.c1 = 2*(q.y*q.z - q.x*q.w);
m.c2 = 1 - 2*(q.x*q.x + q.y*q.y);
return m;
}
Float4x4 mat4MtwInverse(Float4x4 m){
Float4x4 i = mat4TransposeMat3(m);
i.d.vec3 = fvec3Negate(m.d.vec3);
return i;
}
void mat4Print(Float4x4 m){
for (int i = 0; i < 4; i++){
printf("%f %f %f %f\n",m.arr[0*4+i],m.arr[1*4+i],m.arr[2*4+i],m.arr[3*4+i]);
}
}
FVec3 fvec3Rotated(FVec3 v, FVec3 euler){
return mat4MulFVec4(eulerToFloat4x4(euler),(FVec4){v.x,v.y,v.z,0.0f}).vec3;
}
FVec3 normalFromTriangle(FVec3 a, FVec3 b, FVec3 c){
return fvec3Norm(fvec3Cross(fvec3Sub(b,a),fvec3Sub(c,a)));
}
int manhattanDistance(IVec3 a, IVec3 b){
return abs(a.x-b.x)+abs(a.y-b.y)+abs(a.z-b.z);
}
Float4x4 getVP(Camera *c){
return mat4Mul(mat4Persp(c->fov,c->aspect,0.01f,1024.0f),mat4Mul(mat4Transpose(eulerToFloat4x4(c->euler)),mat4Pos(fvec3Scale(c->position,-1))));
}
void rotateCamera(Camera *c, float dx, float dy, float sens){
c->euler.y += sens * dx;
c->euler.x += sens * dy;
c->euler = clampEuler(c->euler);
}
//The following SAT implementation is modified from https://stackoverflow.com/a/52010428
bool getSeparatingPlane(FVec3 RPos, FVec3 Plane, FVec3 *aNormals, FVec3 aHalfExtents, FVec3 *bNormals, FVec3 bHalfExtents){
return (fabsf(fvec3Dot(RPos,Plane)) > 
(fabsf(fvec3Dot(fvec3Scale(aNormals[0],aHalfExtents.x),Plane)) +
fabsf(fvec3Dot(fvec3Scale(aNormals[1],aHalfExtents.y),Plane)) +
fabsf(fvec3Dot(fvec3Scale(aNormals[2],aHalfExtents.z),Plane)) +
fabsf(fvec3Dot(fvec3Scale(bNormals[0],bHalfExtents.x),Plane)) + 
fabsf(fvec3Dot(fvec3Scale(bNormals[1],bHalfExtents.y),Plane)) +
fabsf(fvec3Dot(fvec3Scale(bNormals[2],bHalfExtents.z),Plane))));
}
bool OBBsColliding(OBB *a, OBB *b){
FVec3 RPos = fvec3Sub(b->position,a->position);
Float4x4 aRot = eulerToFloat4x4(a->euler);
FVec3 aNormals[3] = {
aRot.a.vec3,
aRot.b.vec3,
aRot.c.vec3
};
Float4x4 bRot = eulerToFloat4x4(b->euler);
FVec3 bNormals[3] = {
bRot.a.vec3,
bRot.b.vec3,
bRot.c.vec3
};
return !(getSeparatingPlane(RPos,aNormals[0],aNormals,a->halfExtents,bNormals,b->halfExtents) ||
getSeparatingPlane(RPos,aNormals[1],aNormals,a->halfExtents,bNormals,b->halfExtents) ||
getSeparatingPlane(RPos,aNormals[2],aNormals,a->halfExtents,bNormals,b->halfExtents) ||
getSeparatingPlane(RPos,bNormals[0],aNormals,a->halfExtents,bNormals,b->halfExtents) ||
getSeparatingPlane(RPos,bNormals[1],aNormals,a->halfExtents,bNormals,b->halfExtents) ||
getSeparatingPlane(RPos,bNormals[2],aNormals,a->halfExtents,bNormals,b->halfExtents) ||
getSeparatingPlane(RPos,fvec3Cross(aNormals[0],bNormals[0]),aNormals,a->halfExtents,bNormals,b->halfExtents) ||
getSeparatingPlane(RPos,fvec3Cross(aNormals[0],bNormals[1]),aNormals,a->halfExtents,bNormals,b->halfExtents) ||
getSeparatingPlane(RPos,fvec3Cross(aNormals[0],bNormals[2]),aNormals,a->halfExtents,bNormals,b->halfExtents) ||
getSeparatingPlane(RPos,fvec3Cross(aNormals[1],bNormals[0]),aNormals,a->halfExtents,bNormals,b->halfExtents) ||
getSeparatingPlane(RPos,fvec3Cross(aNormals[1],bNormals[1]),aNormals,a->halfExtents,bNormals,b->halfExtents) ||
getSeparatingPlane(RPos,fvec3Cross(aNormals[1],bNormals[2]),aNormals,a->halfExtents,bNormals,b->halfExtents) ||
getSeparatingPlane(RPos,fvec3Cross(aNormals[2],bNormals[0]),aNormals,a->halfExtents,bNormals,b->halfExtents) ||
getSeparatingPlane(RPos,fvec3Cross(aNormals[2],bNormals[1]),aNormals,a->halfExtents,bNormals,b->halfExtents) ||
getSeparatingPlane(RPos,fvec3Cross(aNormals[2],bNormals[2]),aNormals,a->halfExtents,bNormals,b->halfExtents));
}
*/

/******************* Files */
char *LoadFileA(char *path, size_t *size){
	FILE *f = fopen(path,"rb");
	if (!f) FatalErrorA("File not found: %s",path);
	fseek(f,0,SEEK_END);
	*size = ftell(f);
	fseek(f,0,SEEK_SET);
	char *buf = MallocOrDie(*size);
	fread(buf,*size,1,f);
	fclose(f);
	return buf;
}
char *LoadFileW(WCHAR *path, size_t *size){
	FILE *f = _wfopen(path,L"rb");
	if (!f) FatalErrorW(L"File not found: %s",path);
	fseek(f,0,SEEK_END);
	*size = ftell(f);
	fseek(f,0,SEEK_SET);
	char *buf = MallocOrDie(*size);
	fread(buf,*size,1,f);
	fclose(f);
	return buf;
}
int CharIsSuitableForFileName(char c){
	return (c==' ') || ((','<=c)&&(c<='9')) || (('A'<=c)&&(c<='Z')) || (('_'<=c)&&(c<='z'));
}
BOOL FileOrFolderExists(LPCTSTR path){
	return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

/***************************** Image */
typedef struct {
	int width,height,rowPitch;
	uint32_t *pixels;
} Image;
void LoadImageFromFile(Image *img, WCHAR *path, bool flip){
	static IWICImagingFactory2 *ifactory = 0;
	if (!ifactory && FAILED(CoCreateInstance(&CLSID_WICImagingFactory2,0,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory2,&ifactory))){
		FatalErrorW(L"LoadImageFromFile: failed to create IWICImagingFactory2");
	}
	IWICBitmapDecoder *pDecoder = 0;
	if (S_OK != ifactory->lpVtbl->CreateDecoderFromFilename(ifactory,path,0,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&pDecoder)){
		FatalErrorW(L"LoadImageFromFile: file not found: %s",path);
	}
	IWICBitmapFrameDecode *pFrame = 0;
	pDecoder->lpVtbl->GetFrame(pDecoder,0,&pFrame);
	IWICBitmapSource *convertedSrc = 0;
	WICConvertBitmapSource(&GUID_WICPixelFormat32bppRGBA,pFrame,&convertedSrc);
	convertedSrc->lpVtbl->GetSize(convertedSrc,&img->width,&img->height);
	uint32_t size = img->width*img->height*sizeof(uint32_t);
	img->rowPitch = img->width*sizeof(uint32_t);
	img->pixels = MallocOrDie(size);
	if (flip){
		IWICBitmapFlipRotator *pFlipRotator;
		ifactory->lpVtbl->CreateBitmapFlipRotator(ifactory,&pFlipRotator);
		pFlipRotator->lpVtbl->Initialize(pFlipRotator,convertedSrc,WICBitmapTransformFlipVertical);
		if (S_OK != pFlipRotator->lpVtbl->CopyPixels(pFlipRotator,0,img->rowPitch,size,img->pixels)){
			FatalErrorW(L"LoadImageFromFile: %s CopyPixels failed",path);
		}
		pFlipRotator->lpVtbl->Release(pFlipRotator);
	} else {
		if (S_OK != convertedSrc->lpVtbl->CopyPixels(convertedSrc,0,img->rowPitch,size,img->pixels)){
			FatalErrorW(L"LoadImageFromFile: %s CopyPixels failed",path);
		}
	}
	convertedSrc->lpVtbl->Release(convertedSrc);
	pFrame->lpVtbl->Release(pFrame);
	pDecoder->lpVtbl->Release(pDecoder);
}

/********************** DarkViewer 2 */
WCHAR gpath[MAX_PATH+16];
HWND gwnd;
HDC hdc;
HCURSOR cursorArrow, cursorFinger, cursorPan;
int dpi;
int dpiScale(int value){
	return (int)((float)value * dpi / 96);
}
void CenterRectInRect(RECT *inner, RECT *outer){
	int iw = inner->right - inner->left;
	int ih = inner->bottom - inner->top;
	int ow = outer->right - outer->left;
	int oh = outer->bottom - outer->top;
	inner->left = outer->left + (ow-iw)/2;
	inner->top = outer->top + (oh-ih)/2;
	inner->right = inner->left + iw;
	inner->bottom = inner->top + ih;
}
BOOL WindowMaximized(HWND handle){
	WINDOWPLACEMENT p = {0};
	p.length = sizeof(WINDOWPLACEMENT);
	if (GetWindowPlacement(handle,&p)) return p.showCmd == SW_SHOWMAXIMIZED;
	return FALSE;
}
int scale = 1;
bool interpolation = false;
float pos[3];
bool pan = false;
POINT panPoint;
float originalPos[3];
typedef struct {
	RECT rect;
	WCHAR *string;
	void (*func)(void);
} Button;
void Close(){
	PostMessageA(gwnd,WM_CLOSE,0,0);
}
void Maximize(){
	ShowWindow(gwnd,WindowMaximized(gwnd) ? SW_NORMAL : SW_MAXIMIZE);
}
void Minimize(){
	ShowWindow(gwnd,SW_MINIMIZE);
}
Button bMinimize = {.func = Minimize},bMaximize = {.func = Maximize},bClose = {.func = Close};
Button *hoveredButton;
RECT rTitlebar,rCaption,rClient;
HBRUSH bSystem,bSystemHovered,bCloseHovered,bCaption;
HPEN pUnfocused,pFocused;

void CalcRects(){
	HTHEME theme = OpenThemeData(gwnd,L"WINDOW");
	SIZE size = {0};
	GetThemePartSize(theme,0,WP_CAPTION,CS_ACTIVE,0,TS_TRUE,&size);
	CloseThemeData(theme);
	GetClientRect(gwnd,&rClient);
	rTitlebar = rClient;
	rTitlebar.bottom = dpiScale(size.cy) + 2;
	rClient.top = rTitlebar.bottom;

	int buttonWidth = dpiScale(47);
	int buttonHeight = rTitlebar.bottom;

	bClose.rect = rTitlebar;
	bClose.rect.left = bClose.rect.right - buttonWidth;

	bMaximize.rect = bClose.rect;
	bMaximize.rect.left -= buttonWidth;
	bMaximize.rect.right -= buttonWidth;

	bMinimize.rect = bMaximize.rect;
	bMinimize.rect.left -= buttonWidth;
	bMinimize.rect.right -= buttonWidth;

	rCaption = rTitlebar;
	rCaption.right = bMinimize.rect.left;
}

LRESULT WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
	switch (uMsg){
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		case WM_NCCALCSIZE:{
			if (!wParam) return DefWindowProc(hwnd,uMsg,wParam,lParam);
			dpi = GetDpiForWindow(hwnd);
			int fx = GetSystemMetricsForDpi(SM_CXFRAME,dpi);
			int fy = GetSystemMetricsForDpi(SM_CYFRAME,dpi);
			int pad = GetSystemMetricsForDpi(SM_CXPADDEDBORDER,dpi);
			NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS *)lParam;
			RECT *rr = params->rgrc;
			rr->right -= fx + pad;
			rr->left += fx + pad;
			rr->bottom -= fy + pad;
			if (WindowMaximized(hwnd)) rr->top += pad;
			return 0;
		}
		case WM_CREATE:{
			gwnd = hwnd;
			RECT wr;
			GetWindowRect(hwnd,&wr);
			SetWindowPos(hwnd,0,wr.left,wr.top,wr.right-wr.left,wr.bottom-wr.top,SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOSIZE);
			break;
		}
		case WM_ACTIVATE:{
			CalcRects();
			InvalidateRect(hwnd,&rTitlebar,0);
			return DefWindowProcW(hwnd,uMsg,wParam,lParam);
		}
		case WM_NCHITTEST:{
			LRESULT hit = DefWindowProcW(hwnd,uMsg,wParam,lParam);
			switch (hit){
				case HTNOWHERE:
				case HTRIGHT:
				case HTLEFT:
				case HTTOPLEFT:
				case HTTOP:
				case HTTOPRIGHT:
				case HTBOTTOMRIGHT:
				case HTBOTTOM:
				case HTBOTTOMLEFT:
					return hit;
			}
			int fy = GetSystemMetricsForDpi(SM_CYFRAME,dpi);
			int pad = GetSystemMetricsForDpi(SM_CXPADDEDBORDER,dpi);
			POINT cursor = {LOWORD(lParam),HIWORD(lParam)};
			ScreenToClient(hwnd,&cursor);
			if (cursor.y >= 0 && cursor.y < fy + pad) return HTTOP;
			if (cursor.y < rTitlebar.bottom) return HTCAPTION;
			return HTCLIENT;
		}
		case WM_NCMOUSEMOVE:case WM_MOUSEMOVE:{
			POINT cursor;
			GetCursorPos(&cursor);
			ScreenToClient(hwnd,&cursor);
			if (PtInRect(&bMinimize.rect,cursor)){
				if (hoveredButton != &bMinimize){
					hoveredButton = &bMinimize;
					InvalidateRect(hwnd,0,0);
				}
			} else if (PtInRect(&bMaximize.rect,cursor)){
				if (hoveredButton != &bMaximize){
					hoveredButton = &bMaximize;
					InvalidateRect(hwnd,0,0);
				}
			} else if (PtInRect(&bClose.rect,cursor)){
				if (hoveredButton != &bClose){
					hoveredButton = &bClose;
					InvalidateRect(hwnd,0,0);
				}
			} else if (hoveredButton){
				hoveredButton = 0;
				InvalidateRect(hwnd,0,0);
			}
			TRACKMOUSEEVENT tme = {sizeof(tme),uMsg == WM_NCMOUSEMOVE ? TME_LEAVE|TME_NONCLIENT : TME_LEAVE,hwnd,0};
			TrackMouseEvent(&tme);
			return 0;
		}
		case WM_NCMOUSELEAVE:case WM_MOUSELEAVE:
			if (hoveredButton){
				hoveredButton = 0;
				InvalidateRect(hwnd,0,0);
			}
			return 0;
		case WM_NCLBUTTONDOWN:case WM_NCLBUTTONDBLCLK:case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:
			if (hoveredButton && hoveredButton->func){
				hoveredButton->func();
				return 0;
			}
			break;
		case WM_PAINT:{
			PAINTSTRUCT ps;
			HDC phdc = BeginPaint(hwnd,&ps), hdc;
			HPAINTBUFFER pb = BeginBufferedPaint(phdc,&ps.rcPaint,BPBF_COMPATIBLEBITMAP,NULL,&hdc);

			CalcRects();
			BOOL focus = GetFocus();
			SelectObject(hdc,focus ? pFocused : pUnfocused);

			FillRect(hdc,&rCaption,bCaption);
			FillRect(hdc,&bMinimize.rect,hoveredButton == &bMinimize ? bSystemHovered : bSystem);
			FillRect(hdc,&bMaximize.rect,hoveredButton == &bMaximize ? bSystemHovered : bSystem);
			FillRect(hdc,&bClose.rect,hoveredButton == &bClose ? bSystemHovered : bSystem);
			
			int iconWidth = dpiScale(10);
			RECT ir = {0,0,iconWidth,iconWidth};
			CenterRectInRect(&ir,&bClose.rect);
			MoveToEx(hdc,ir.left,ir.top,NULL);
			LineTo(hdc,ir.right + 1,ir.bottom + 1);
			MoveToEx(hdc,ir.left,ir.bottom,NULL);
			LineTo(hdc,ir.right + 1,ir.top - 1);

			SelectObject(hdc,GetStockObject(HOLLOW_BRUSH));
			ir.left = 0;
			ir.top = 0;
			ir.right = iconWidth;
			ir.bottom = iconWidth;
			CenterRectInRect(&ir,&bMaximize.rect);
			if (WindowMaximized(hwnd)){
				Rectangle(hdc,ir.left+2,ir.top-2,ir.right+2,ir.bottom-2);
				FillRect(hdc,&ir,hoveredButton == &bMaximize ? bSystemHovered : bSystem);
			}
			Rectangle(hdc,ir.left,ir.top,ir.right,ir.bottom);

			ir.left = 0;
			ir.top = 0;
			ir.right = iconWidth;
			ir.bottom = 1;
			CenterRectInRect(&ir,&bMinimize.rect);
			MoveToEx(hdc,ir.left,ir.top,NULL);
			LineTo(hdc,ir.right,ir.top);

			EndBufferedPaint(pb,TRUE);
			EndPaint(hwnd,&ps);
			return 0;
		}
	}
	return DefWindowProcW(hwnd,uMsg,wParam,lParam);
}
WNDCLASSW wndclass = {
	.style = CS_HREDRAW|CS_VREDRAW,
	.lpfnWndProc = (WNDPROC)WindowProc,
	.lpszClassName = L"DarkViewer 2"
};
#if _DEBUG
void main(){
#else
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd){
#endif
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	CoInitialize(0);

	int argc;
	WCHAR **argv = CommandLineToArgvW(GetCommandLineW(),&argc);
	if (argc==2){
		//LoadImg(argv[1]);
	}

	bSystem = CreateSolidBrush(RGB(9,18,25));
	bSystemHovered = CreateSolidBrush(RGB(40,40,40));
	bCloseHovered = CreateSolidBrush(RGB(0xCC,0,0));
	bCaption = CreateSolidBrush(RGB(0,120,0));
	
	pUnfocused = CreatePen(PS_SOLID,1,RGB(127,127,127));
	pFocused = CreatePen(PS_SOLID,1,RGB(255,141,61));

	wndclass.hInstance = GetModuleHandleW(0);
	RegisterClassW(&wndclass);
	RECT wndrect = {0,0,800,600};
	AdjustWindowRect(&wndrect,WS_OVERLAPPEDWINDOW,FALSE);
	CreateWindowExW(WS_EX_APPWINDOW,wndclass.lpszClassName,wndclass.lpszClassName,WS_OVERLAPPEDWINDOW|WS_VISIBLE,(GetSystemMetrics(SM_CXSCREEN)-wndrect.right)/2,(GetSystemMetrics(SM_CYSCREEN)-wndrect.bottom)/2,wndrect.right,wndrect.bottom,0,0,wndclass.hInstance,0);

	cursorArrow = LoadCursorA(0,IDC_ARROW);
	cursorFinger = LoadCursorA(0,IDC_HAND);
	cursorPan = LoadCursorA(0,IDC_SIZEALL);
	SetCursor(cursorArrow);

	MSG msg;
	while (GetMessageW(&msg,0,0,0)){
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	CoUninitialize();
}