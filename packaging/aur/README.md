# AUR packaging for freepiano-linux

These files are a ready-to-submit AUR package for
<https://github.com/QPRS-6/freepiano-linux>.

The package name was unused on AUR when this was prepared (2026-11-06).
Re-check <https://aur.archlinux.org/packages?K=freepiano> before the first push.

## What you need

1. An AUR account: <https://aur.archlinux.org/register>
2. An SSH key registered on the AUR account:
   <https://aur.archlinux.org/account> (paste the contents of the `.pub` file)
3. `base-devel` and `git` on the build machine.

```bash
ssh-keygen -t ed25519 -C "aur"
cat ~/.ssh/id_ed25519.pub
ssh aur@aur.archlinux.org          # should say: Interactive shell is disabled.
```

## First submission

```bash
mkdir freepiano-linux
cd freepiano-linux
git init -b master
git remote add origin ssh://aur@aur.archlinux.org/freepiano-linux.git

cp /path/to/freepiano-linux/packaging/aur/PKGBUILD .
cp /path/to/freepiano-linux/packaging/aur/.SRCINFO .

git add PKGBUILD .SRCINFO
git commit -m "Initial import: freepiano-linux 1.0.12-1"
git push origin master
```

If the AUR repository already exists, use
`git clone ssh://aur@aur.archlinux.org/freepiano-linux.git` instead of the
`git init`/`git remote add` pair.

The first push creates the package. No pull request is needed. After a short
delay it appears at <https://aur.archlinux.org/packages/freepiano-linux>.

## Test locally first

```bash
cd packaging/aur
makepkg --printsrcinfo > .SRCINFO   # regenerate after editing PKGBUILD
makepkg -f                          # builds the package
makepkg -si                         # optional: install it system-wide
```

## Updating for a new release

1. Tag and publish the new GitHub release with a source tarball.
2. Update `pkgver`, the source URL and `sha256sums` (or run `updpkgsums`).
3. Regenerate metadata and push:

```bash
updpkgsums
makepkg --printsrcinfo > .SRCINFO
git commit -am "upgpkg: freepiano-linux <version>"
git push
```
