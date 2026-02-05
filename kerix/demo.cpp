// kerix
// Copyright (C) 2025-2026  Cole Reynolds
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <kerix/kerix.hpp>
#include <print>

namespace kerix {

struct Foo {

    void do_thing(int val) { std::println("in do_thing with {}", val); }
};

struct Bar {
    signal<void(int)> merp;
};

} // namespace kerix

using kerix::Bar;
using kerix::Foo;

int main()
{
    Foo* f = new Foo;
    Bar b;

    b.merp.connect<&Foo::do_thing>(f);

    b.merp(1);
    b.merp(2);
    b.merp(3);

    b.merp.post(4);
    b.merp.post(5);
    b.merp.post(6);
    b.merp.flush();

    delete f;

    b.merp(8);
}
