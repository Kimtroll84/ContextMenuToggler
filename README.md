# Context Menu Toggler

Windows 11의 우클릭 메뉴를 Windows 10 스타일의 클래식 메뉴로 전환하고, 필요할 때 Windows 11 기본 메뉴로 복원하는 포터블 앱입니다.

2026년 11월 10일 정기 보안 업데이트 **KB5132061**이 적용된 Windows 11에서도 클래식 메뉴를 사용할 수 있도록, 26H2 빌드 `26300.9974`부터 두 레지스트리 항목을 사용하는 방식을 적용합니다. [Microsoft 업데이트 기록](https://support.microsoft.com/en-us/servicing/os/windows-11/2026/11/kb5132061-windows-11-24h2-25h2-26h2-security-update)

## 다운로드 및 사용법

1. [앱 배포 페이지](https://github.com/Kimtroll84/ContextMenuToggler/releases)에서 `ContextMenuToggler.zip`을 내려받아 압축을 푼 뒤, `ContextMenuToggler.exe`를 실행합니다. Windows 11 x64용 단일 실행 파일이며 설치와 관리자 권한이 필요하지 않습니다.
2. **Windows 클래식 메뉴 적용** 또는 **Windows 11 기본 메뉴 복원**을 선택합니다.
3. 바로 반영하려면 **지금 재시작**을 선택해 Windows 탐색기를 다시 시작합니다. **나중에**를 선택해도 설정은 유지됩니다.

상태창의 말풍선은 현재 사용자의 레지스트리 설정을 보여줍니다. 소녀 아이콘을 누르면 제작자와 버전, 배포 페이지를 볼 수 있습니다. Windows 표시 언어가 한국어이면 한국어로, 그 외에는 영어로 표시됩니다.

## 호환성

| Windows 11 빌드 | 동작 |
| --- | --- |
| `22000`, `22621`, `22631` | 기존 CLSID를 사용합니다. |
| `26300.9974`부터 같은 계열의 후속 업데이트 및 더 높은 클라이언트 빌드 | 두 CLSID를 사용합니다. |
| 그 외 빌드 | 확인 필요 상태를 표시하고 변경을 막습니다. |

두 항목을 사용하는 빌드에 기존 방식의 설정만 남아 있으면 **설정 불완전**으로 표시합니다. 클래식 메뉴 적용으로 빠진 항목을 채우거나, 기본 메뉴 복원으로 기존 항목까지 제거할 수 있습니다. 앱은 현재 사용자 범위에서 인식한 우회 설정만 제거합니다. 예상 밖의 값이 발견되면 **확인 필요**를 표시하고 해당 값을 보존합니다.

## 빌드

x64 Developer PowerShell에서 Windows SDK와 MSVC를 사용해 아래 명령을 실행합니다. 기본 출력은 `.build\ContextMenuToggler.exe`입니다.

```powershell
.\build.ps1
```

제작자 [Troll_Kim](https://github.com/Kimtroll84)
