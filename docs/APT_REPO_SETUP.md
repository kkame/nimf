# GitHub Pages APT Repository 설정 가이드 (`kkame/nimf`)

이 저장소는 GitHub Actions(`.github/workflows/release.yml`)를 통해 태그(`v*`)가 푸시될 때 Ubuntu(24.04 Noble / 26.04 Resolute) 및 Debian(12 Bookworm / 13 Trixie) `.deb` 패키지를 빌드하고, `gh-pages` 브랜치에 GPG 서명된 APT 저장소를 자동 배포합니다.

## 1. APT 서명용 GPG 키 생성 (최초 1회)

로컬 PC에서 비밀번호가 없거나(CI 자동화용) 비밀번호가 있는 GPG 키를 생성합니다.

```bash
gpg --batch --passphrase '' --quick-generate-key "kkame Nimf APT Repo <kkame@users.noreply.github.com>" rsa4096 sign never
```

생성된 키 ID 확인:

```bash
gpg --list-secret-keys --keyid-format=long
```

비밀키를 ASCII Armor 형식으로 추출:

```bash
gpg --armor --export-secret-keys <KEY_ID>
```

## 2. GitHub Repository Secrets 등록

`https://github.com/kkame/nimf/settings/secrets/actions` 에서 아래 Secret을 추가합니다.

- **`APT_GPG_PRIVATE_KEY`** (필수): 위 `gpg --armor --export-secret-keys <KEY_ID>` 출력 전체 (`-----BEGIN PGP PRIVATE KEY BLOCK-----` ~ `-----END PGP PRIVATE KEY BLOCK-----`)
- **`APT_GPG_PASSPHRASE`** (선택): 키에 비밀번호를 설정한 경우에만 등록 (비밀번호 없이 생성했다면 생략 가능)

## 3. GitHub Pages 활성화 (최초 1회)

1. 첫 릴리즈 워크플로우 실행 후 `gh-pages` 브랜치가 자동 생성됩니다.
2. `https://github.com/kkame/nimf/settings/pages` 로 이동합니다.
3. **Build and deployment → Source**를 `Deploy from a branch`로 선택하고, Branch를 **`gh-pages` / `/ (root)`**로 설정한 뒤 저장합니다.

## 4. 배포 실행

```bash
git push -u origin master
git push origin v1.4.21
```

또는 GitHub Actions 탭에서 **Multi-Platform Release & APT Repository → Run workflow**로 수동 실행할 수도 있습니다.

## 5. Ubuntu / Debian 사용자 설치 방법

```bash
# 1) GPG 키 등록
curl -fsSL https://kkame.github.io/nimf/gpg.key | sudo gpg --dearmor -o /usr/share/keyrings/kkame-nimf.gpg

# 2) APT 저장소 추가 (noble / resolute / bookworm / trixie 자동 감지)
echo "deb [signed-by=/usr/share/keyrings/kkame-nimf.gpg] https://kkame.github.io/nimf $(. /etc/os-release && echo "$VERSION_CODENAME") main" | sudo tee /etc/apt/sources.list.d/kkame-nimf.list

# 3) 설치 및 이후 자동 업그레이드
sudo apt update
sudo apt install -y nimf
```
