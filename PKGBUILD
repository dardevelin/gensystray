# Maintainer: Darcy Bras da Silva <stardevelin@gmail.com>
pkgname=gensystray
pkgver=2.6.0
pkgrel=1
pkgdesc='Configurable system tray menu with live items, glob populate, watch events, and signal-slot IPC'
arch=('x86_64' 'aarch64')
url='https://github.com/dardevelin/gensystray'
license=('GPL3')
depends=('gtk3')
makedepends=('git' 'gcc' 'pkg-config')
source=("git+${url}.git#tag=v${pkgver}")
sha256sums=('SKIP')

prepare() {
    cd "$pkgname"
    git submodule update --init --recursive
}

build() {
    cd "$pkgname"
    make release
}

package() {
    cd "$pkgname"
    make install DESTDIR="$pkgdir" PREFIX=/usr
}
