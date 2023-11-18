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
void ImageFromFile(Image *img, WCHAR *path, bool flip, bool bgr){
	static IWICImagingFactory2 *ifactory = 0;
	if (!ifactory && FAILED(CoCreateInstance(&CLSID_WICImagingFactory2,0,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory2,&ifactory))){
		FatalErrorW(L"ImageFromFile: failed to create IWICImagingFactory2");
	}
	IWICBitmapDecoder *pDecoder = 0;
	if (S_OK != ifactory->lpVtbl->CreateDecoderFromFilename(ifactory,path,0,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&pDecoder)){
		FatalErrorW(L"ImageFromFile: file not found: %s",path);
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
			FatalErrorW(L"ImageFromFile: %s CopyPixels failed",path);
		}
		pFlipRotator->lpVtbl->Release(pFlipRotator);
	} else {
		if (S_OK != convertedSrc->lpVtbl->CopyPixels(convertedSrc,0,img->rowPitch,size,img->pixels)){
			FatalErrorW(L"ImageFromFile: %s CopyPixels failed",path);
		}
	}
	if (bgr){
		for (int y = 0; y < img->height; y++){
			for (int x = 0; x < img->width; x++){
				uint8_t *p = img->pixels+y*img->width+x;
				uint8_t r = p[0];
				p[0] = p[2];
				p[2] = r;
			}
		}
	}
	convertedSrc->lpVtbl->Release(convertedSrc);
	pFrame->lpVtbl->Release(pFrame);
	pDecoder->lpVtbl->Release(pDecoder);
}

/********************** DarkViewer 2 */
WCHAR gpath[MAX_PATH+64];
WCHAR currentFolder[MAX_PATH+64];
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
LIST_IMPLEMENTATION(LPWSTR)
Image image;
LPWSTRList imagePaths;
int imageIndex;
void GetImagesInFolder(LPWSTRList *list, WCHAR *path){
	LIST_FREE(list);
	WIN32_FIND_DATAW fd;
	HANDLE hFind = 0;
	if ((hFind = FindFirstFileW(path,&fd)) == INVALID_HANDLE_VALUE) FatalErrorW(L"GetImagesInFolder: Folder not found: %s",path);
	do {
		//FindFirstFile will always return "." and ".." as the first two directories.
		if (wcscmp(fd.cFileName,L".") && wcscmp(fd.cFileName,L"..") && !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)){
			size_t len = wcslen(fd.cFileName);
			if (len > 4 && (!wcscmp(fd.cFileName+len-4,L".jpg") || !wcscmp(fd.cFileName+len-4,L".png"))){
				LPWSTR *s = LPWSTRListMakeRoom(list,1);
				*s = MallocOrDie((len+1)*sizeof(WCHAR));
				wcscpy(*s,fd.cFileName);
				list->used++;
			}
		}
	} while (FindNextFileW(hFind,&fd));
	FindClose(hFind);
}
void OpenImageFromNewFolder(WCHAR *path){
	if (image.pixels) free(image.pixels);
	ImageFromFile(&image,path,false,true);
	_snwprintf(gpath,COUNT(gpath),L"%s - DarkViewer 2",path);
	SetWindowTextW(gwnd,gpath);
	wcscpy(gpath,path);
	wcscpy(currentFolder,path);
	WCHAR *s = wcsrchr(gpath,L'\\');
	currentFolder[s-gpath+1] = 0;
	WCHAR *name = (s-gpath)+path+1;
	s[1] = L'*';
	s[2] = 0;
	GetImagesInFolder(&imagePaths,gpath);
	for (int i = 0; i < imagePaths.used; i++){
		if (!wcscmp(imagePaths.elements[i],name)){
			imageIndex = i;
			break;
		}
	}
}
void OpenImage(){
	IFileDialog *pfd;
	IShellItem *psi;
	PWSTR path = 0;
	COMDLG_FILTERSPEC fs = {L"Image files", L"*.png;*.jpg;*.jpeg"};
	if (SUCCEEDED(CoCreateInstance(&CLSID_FileOpenDialog,0,CLSCTX_INPROC_SERVER,&IID_IFileOpenDialog,&pfd))){
		pfd->lpVtbl->SetFileTypes(pfd,1,&fs); 
		pfd->lpVtbl->SetTitle(pfd,L"Open Image");
		pfd->lpVtbl->Show(pfd,gwnd);
		if (SUCCEEDED(pfd->lpVtbl->GetResult(pfd,&psi))){
			if (SUCCEEDED(psi->lpVtbl->GetDisplayName(psi,SIGDN_FILESYSPATH,&path))){
				OpenImageFromNewFolder(path);
				InvalidateRect(gwnd,0,0);
				CoTaskMemFree(path);
			}
			psi->lpVtbl->Release(psi);
		}
		pfd->lpVtbl->Release(pfd);
	}
}
void ToggleInterpolation();
Button
	buttonMinimize = {.func = Minimize},
	buttonMaximize = {.func = Maximize},
	buttonClose = {.func = Close},
	buttonOpenImage = {.string = L"Open Image", .func = OpenImage},
	buttonInterpolation = {.string = L"Interpolation: Off", .func = ToggleInterpolation};
void ToggleInterpolation(){
	interpolation = !interpolation;
	buttonInterpolation.string = interpolation ? L"Interpolation: On" : L"Interpolation: Off";
	InvalidateRect(gwnd,0,0);
}
Button *hoveredButton;
RECT rTitlebar,rCaption,rClient;
HBRUSH brushSystem,brushSystemHovered,brushCloseHovered,brushCaption;
HPEN penUnfocused,penFocused;
HFONT captionfont;

typedef struct {
	BITMAPINFOHEADER    bmiHeader;
	RGBQUAD             bmiColors[4];
} BITMAPINFO_TRUECOLOR32;

void CalcRects(HDC hdc){
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

	buttonClose.rect = rTitlebar;
	buttonClose.rect.left = buttonClose.rect.right - buttonWidth;

	buttonMaximize.rect = buttonClose.rect;
	buttonMaximize.rect.left -= buttonWidth;
	buttonMaximize.rect.right -= buttonWidth;

	buttonMinimize.rect = buttonMaximize.rect;
	buttonMinimize.rect.left -= buttonWidth;
	buttonMinimize.rect.right -= buttonWidth;

	RECT r = {0};
	DrawTextW(hdc,buttonOpenImage.string,wcslen(buttonOpenImage.string),&r,DT_NOPREFIX|DT_CALCRECT);
	buttonOpenImage.rect = rTitlebar;
	buttonOpenImage.rect.right = r.right + dpiScale(16);

	r.left = 0;
	r.top = 0;
	r.right = 0;
	r.bottom = 0;
	DrawTextW(hdc,buttonInterpolation.string,wcslen(buttonInterpolation.string),&r,DT_NOPREFIX|DT_CALCRECT);
	buttonInterpolation.rect = rTitlebar;
	buttonInterpolation.rect.left = buttonOpenImage.rect.right;
	buttonInterpolation.rect.right = buttonInterpolation.rect.left + r.right + dpiScale(16);

	rCaption = rTitlebar;
	rCaption.left = buttonInterpolation.rect.right;
	rCaption.right = buttonMinimize.rect.left;
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
			DragAcceptFiles(hwnd,1);
			gwnd = hwnd;
			RECT wr;
			GetWindowRect(hwnd,&wr);
			SetWindowPos(hwnd,0,wr.left,wr.top,wr.right-wr.left,wr.bottom-wr.top,SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOSIZE);
			break;
		}
		case WM_ACTIVATE:{
			HDC hdc = GetDC(hwnd);
			HFONT oldfont = SelectObject(hdc,captionfont);
			SetTextColor(hdc,RGB(255,255,255));
			SetBkMode(hdc,TRANSPARENT);
			CalcRects(hdc);
			SelectObject(hdc,oldfont);
			ReleaseDC(hwnd,hdc);
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
			if (PtInRect(&rClient,cursor)){
				SetCursor(scale > 1 ? cursorPan : cursorArrow);
			} else if (PtInRect(&rTitlebar,cursor) && GetCursor()==cursorPan){
				SetCursor(cursorArrow);
			}
			if (pan){
				pos[0] = originalPos[0] + (cursor.x - panPoint.x);
				pos[1] = originalPos[1] + (cursor.y - panPoint.y);
				InvalidateRect(hwnd,0,0);
			} else {
				if (PtInRect(&buttonMinimize.rect,cursor)){
					if (hoveredButton != &buttonMinimize){
						hoveredButton = &buttonMinimize;
						InvalidateRect(hwnd,0,0);
					}
				} else if (PtInRect(&buttonMaximize.rect,cursor)){
					if (hoveredButton != &buttonMaximize){
						hoveredButton = &buttonMaximize;
						InvalidateRect(hwnd,0,0);
					}
				} else if (PtInRect(&buttonClose.rect,cursor)){
					if (hoveredButton != &buttonClose){
						hoveredButton = &buttonClose;
						InvalidateRect(hwnd,0,0);
					}
				} else if (PtInRect(&buttonOpenImage.rect,cursor)){
					if (hoveredButton != &buttonOpenImage){
						hoveredButton = &buttonOpenImage;
						InvalidateRect(hwnd,0,0);
					}
				} else if (PtInRect(&buttonInterpolation.rect,cursor)){
					if (hoveredButton != &buttonInterpolation){
						hoveredButton = &buttonInterpolation;
						InvalidateRect(hwnd,0,0);
					}
				} else if (hoveredButton){
					hoveredButton = 0;
					InvalidateRect(hwnd,0,0);
				}
				TRACKMOUSEEVENT tme = {sizeof(tme),uMsg == WM_NCMOUSEMOVE ? TME_LEAVE|TME_NONCLIENT : TME_LEAVE,hwnd,0};
				TrackMouseEvent(&tme);
			}
			return 0;
		}
		case WM_NCMOUSELEAVE:case WM_MOUSELEAVE:
			if (hoveredButton){
				hoveredButton = 0;
				InvalidateRect(hwnd,0,0);
			}
			return 0;
		case WM_NCLBUTTONDOWN:case WM_NCLBUTTONDBLCLK:case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:{
			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(hwnd,&pt);
			if (hoveredButton && hoveredButton->func){
				hoveredButton->func();
				return 0;
			} else if (PtInRect(&rClient,pt) && scale > 1){
				panPoint = pt;
				originalPos[0] = pos[0];
				originalPos[1] = pos[1];
				originalPos[2] = pos[2];
				pan = true;
			}
			break;
		}
		case WM_NCLBUTTONUP:case WM_LBUTTONUP:{
			pan = false;
			return 0;
		}
		case WM_MOUSEWHEEL:{
			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(hwnd,&pt);
			if (PtInRect(&rClient,pt)){
				int oldScale = scale;
				scale = max(1,scale+CLAMP(GET_WHEEL_DELTA_WPARAM(wParam)/WHEEL_DELTA,-1,1));
				if (oldScale != scale){
					pt.y -= rClient.top;
					pt.x -= pos[0];
					pt.y -= pos[1];
					if (scale > oldScale){
						pos[0] -= pt.x / (float)(scale-1);
						pos[1] -= pt.y / (float)(scale-1);
					} else {
						pos[0] += pt.x / (float)(scale+1);
						pos[1] += pt.y / (float)(scale+1);
					}
					InvalidateRect(hwnd,0,0);
					if (GetCursor() != cursorFinger) SetCursor(scale == 1 ? cursorArrow : cursorPan);
				}
			}
			return 0;
		}
		case WM_KEYDOWN:{
			switch (wParam){
				case VK_LEFT:{
					if (image.pixels && imageIndex > 0){
						free(image.pixels);
						imageIndex--;
						_snwprintf(gpath,COUNT(gpath),L"%s%s",currentFolder,imagePaths.elements[imageIndex]);
						ImageFromFile(&image,gpath,0,true);
						wcscat(gpath,L" - DarkViewer 2");
						SetWindowTextW(hwnd,gpath);
						InvalidateRect(hwnd,0,0);
					}
					return 0;
				}
				case VK_RIGHT:{
					if (image.pixels && imageIndex < imagePaths.used-1){
						free(image.pixels);
						imageIndex++;
						_snwprintf(gpath,COUNT(gpath),L"%s%s",currentFolder,imagePaths.elements[imageIndex]);
						ImageFromFile(&image,gpath,0,true);
						wcscat(gpath,L" - DarkViewer 2");
						SetWindowTextW(hwnd,gpath);
						InvalidateRect(hwnd,0,0);
					}
					return 0;
				}
			}
			break;
		}
		case WM_DROPFILES:{
			WCHAR p[MAX_PATH];
			if (DragQueryFileW(wParam,0xFFFFFFFF,0,0) > 1) goto EXIT_DROPFILES;
			DragQueryFileW(wParam,0,p,COUNT(p));
			OpenImageFromNewFolder(p);
			InvalidateRect(hwnd,0,0);
		EXIT_DROPFILES:
			DragFinish(wParam);
			return 0;
		}
		case WM_PAINT:{
			PAINTSTRUCT ps;
			HDC phdc = BeginPaint(hwnd,&ps), hdc;
			HPAINTBUFFER pb = BeginBufferedPaint(phdc,&ps.rcPaint,BPBF_COMPATIBLEBITMAP,NULL,&hdc);

			HFONT oldfont = SelectObject(hdc,captionfont);
			SetTextColor(hdc,RGB(255,255,255));
			SetBkMode(hdc,TRANSPARENT);
			CalcRects(hdc);
			BOOL focus = GetFocus();
			SelectObject(hdc,focus ? penFocused : penUnfocused);

			if (image.pixels){
				float width = min(rClient.right,image.width);
				float height = width * ((float)image.height/(float)image.width);
				float clientHeight = rClient.bottom-rClient.top;
				if (height > clientHeight){
					height = clientHeight;
					width = height * ((float)image.width/(float)image.height);
				}
				width *= scale;
				height *= scale;
				if (scale == 1){
					pos[0] = (rClient.right-(int)width)/2;
					pos[1] = (clientHeight-(int)height)/2;
					pos[2] = -1;
				}

				BITMAPINFO_TRUECOLOR32 bmi = {0};
				bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bmi.bmiHeader.biWidth = image.width;
				bmi.bmiHeader.biHeight = -image.height;
				bmi.bmiHeader.biPlanes = 1;
				bmi.bmiHeader.biCompression = BI_RGB | BI_BITFIELDS;
				bmi.bmiHeader.biBitCount = 32;
				bmi.bmiColors[0].rgbRed = 0xff;
				bmi.bmiColors[1].rgbGreen = 0xff;
				bmi.bmiColors[2].rgbBlue = 0xff;
				SetStretchBltMode(hdc,interpolation ? HALFTONE : COLORONCOLOR);
				StretchDIBits(hdc,pos[0],rClient.top + pos[1],width,height,0,0,image.width,image.height,image.pixels,&bmi,DIB_RGB_COLORS,SRCCOPY);
			}

			FillRect(hdc,&rCaption,brushCaption);
			FillRect(hdc,&buttonMinimize.rect,hoveredButton == &buttonMinimize ? brushSystemHovered : brushSystem);
			FillRect(hdc,&buttonMaximize.rect,hoveredButton == &buttonMaximize ? brushSystemHovered : brushSystem);
			FillRect(hdc,&buttonClose.rect,hoveredButton == &buttonClose ? brushSystemHovered : brushSystem);
			
			int iconWidth = dpiScale(10);
			RECT ir = {0,0,iconWidth,iconWidth};
			CenterRectInRect(&ir,&buttonClose.rect);
			MoveToEx(hdc,ir.left,ir.top,NULL);
			LineTo(hdc,ir.right + 1,ir.bottom + 1);
			MoveToEx(hdc,ir.left,ir.bottom,NULL);
			LineTo(hdc,ir.right + 1,ir.top - 1);

			SelectObject(hdc,GetStockObject(HOLLOW_BRUSH));
			ir.left = 0;
			ir.top = 0;
			ir.right = iconWidth;
			ir.bottom = iconWidth;
			CenterRectInRect(&ir,&buttonMaximize.rect);
			if (WindowMaximized(hwnd)){
				Rectangle(hdc,ir.left+2,ir.top-2,ir.right+2,ir.bottom-2);
				FillRect(hdc,&ir,hoveredButton == &buttonMaximize ? brushSystemHovered : brushSystem);
			}
			Rectangle(hdc,ir.left,ir.top,ir.right,ir.bottom);

			ir.left = 0;
			ir.top = 0;
			ir.right = iconWidth;
			ir.bottom = 1;
			CenterRectInRect(&ir,&buttonMinimize.rect);
			MoveToEx(hdc,ir.left,ir.top,NULL);
			LineTo(hdc,ir.right,ir.top);

			FillRect(hdc,&buttonOpenImage.rect,hoveredButton == &buttonOpenImage ? brushSystemHovered : brushSystem);
			DrawTextW(hdc,buttonOpenImage.string,wcslen(buttonOpenImage.string),&buttonOpenImage.rect,DT_NOPREFIX|DT_SINGLELINE|DT_CENTER|DT_VCENTER);
			FillRect(hdc,&buttonInterpolation.rect,hoveredButton == &buttonInterpolation ? brushSystemHovered : brushSystem);
			DrawTextW(hdc,buttonInterpolation.string,wcslen(buttonInterpolation.string),&buttonInterpolation.rect,DT_NOPREFIX|DT_SINGLELINE|DT_CENTER|DT_VCENTER);
			GetWindowTextW(hwnd,gpath,COUNT(gpath));
			DrawTextW(hdc,gpath,wcslen(gpath),&rCaption,DT_NOPREFIX|DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_END_ELLIPSIS);

			SelectObject(hdc,oldfont);
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

	brushSystem = CreateSolidBrush(RGB(9,18,25));
	brushSystemHovered = CreateSolidBrush(RGB(40,40,40));
	brushCloseHovered = CreateSolidBrush(RGB(0xCC,0,0));
	brushCaption = CreateSolidBrush(RGB(9,18,25));
	
	penUnfocused = CreatePen(PS_SOLID,1,RGB(127,127,127));
	penFocused = CreatePen(PS_SOLID,1,RGB(255,141,61));

	NONCLIENTMETRICSW ncm = {.cbSize = sizeof(ncm)};
	SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(ncm),&ncm,0);
	captionfont = CreateFontIndirectW(&ncm.lfCaptionFont);

	wndclass.hInstance = GetModuleHandleW(0);
	RegisterClassW(&wndclass);
	RECT wndrect = {0,0,800,600};
	AdjustWindowRect(&wndrect,WS_OVERLAPPEDWINDOW,FALSE);
	CreateWindowExW(WS_EX_APPWINDOW,wndclass.lpszClassName,wndclass.lpszClassName,WS_OVERLAPPEDWINDOW,(GetSystemMetrics(SM_CXSCREEN)-wndrect.right)/2,(GetSystemMetrics(SM_CYSCREEN)-wndrect.bottom)/2,wndrect.right,wndrect.bottom,0,0,wndclass.hInstance,0);
	int argc;
	WCHAR **argv = CommandLineToArgvW(GetCommandLineW(),&argc);
	if (argc==2){
		OpenImageFromNewFolder(argv[1]);
	}
	ShowWindow(gwnd,SW_SHOW);

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