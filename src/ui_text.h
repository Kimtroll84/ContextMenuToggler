#pragma once

#include <array>

struct Text {
    std::array<const wchar_t*, 4> status;
    const wchar_t* apply;
    const wchar_t* restore;
    const wchar_t* author;
    const wchar_t* showStatus;
    const wchar_t* releases;
    const wchar_t* restartTitle;
    const wchar_t* restartQuestion;
    const wchar_t* restartNow;
    const wchar_t* later;
    const wchar_t* changeError;
    const wchar_t* restartError;
    const wchar_t* linkError;
};

inline constexpr Text korean{
    {
        L"지금은 윈도우 11 기본 메뉴야.\n클래식으로 바꿔볼까?",
        L"좋아! 클래식 메뉴로 설정돼 있어.",
        L"클래식 설정이 일부만 남아 있어.\n마저 적용할까, 기본으로 돌릴까?",
        L"현재 설정을 확인하기 어려워.\n먼저 확인이 필요해.",
    },
    L"Windows 클래식 메뉴 적용",
    L"Windows 11 기본 메뉴 복원",
    L"제작자:",
    L"상태 대사 보기",
    L"배포 페이지",
    L"Windows 탐색기 재시작",
    L"우클릭 메뉴 설정을 저장했습니다.\r\n지금 Windows 탐색기를 재시작할까요?",
    L"지금 재시작",
    L"나중에",
    L"우클릭 메뉴 설정을 변경하지 못했습니다.",
    L"설정은 저장됐습니다.\r\nWindows 탐색기를 재시작하지 못했습니다.",
    L"링크를 열지 못했습니다.",
};

inline constexpr Text english{
    {
        L"Windows 11 menu selected.\nWant to go classic?",
        L"Nice! The classic menu is selected.",
        L"Classic setup is incomplete.\nFinish setup or restore the default?",
        L"I couldn't confirm the setting.\nIt needs a closer look.",
    },
    L"Apply Classic Menu",
    L"Restore Windows 11 Default Menu",
    L"Created by",
    L"Show menu status",
    L"Releases",
    L"Restart Windows Explorer",
    L"Your context menu setting was saved.\r\nRestart Windows Explorer now?",
    L"Restart now",
    L"Later",
    L"The context menu setting could not be changed.",
    L"The setting was saved.\r\nWindows Explorer could not be restarted.",
    L"The link could not be opened.",
};
