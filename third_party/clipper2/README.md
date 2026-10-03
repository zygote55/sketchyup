Clipper2 2.0.1, upstream commit `21ebba05db8894f0c7217ad35ea518080f324946`.
Source: https://github.com/AngusJohnson/Clipper2
Unmodified library source. Boost Software License 1.0; see LICENSE.
Vendored to make the geometry spike build offline and avoid the host static
library, which omits the triangulation implementation. Triangulation is marked
beta upstream and remains behind the surface adapter and regression tests.
