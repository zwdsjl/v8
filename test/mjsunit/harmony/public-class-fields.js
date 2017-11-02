// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-class-fields

{
  class C {
    static a;
  }

  assertEquals(undefined, C.a);

  let c = new C;
  assertEquals(undefined, c.a);
}

{
  let x = 'a';
  class C {
    static a;
    static b = x;
    static c = 1;
  }

  assertEquals(undefined, C.a);
  assertEquals('a', C.b);
  assertEquals(1, C.c);

  let c = new C;
  assertEquals(undefined, c.a);
  assertEquals(undefined, c.b);
  assertEquals(undefined, c.c);
}

{
  class C {
    static c = this;
    static d = () => this;
  }

  assertEquals(C, C.c);
  assertEquals(C, C.d());

  let c = new C;
  assertEquals(undefined, c.c);
  assertEquals(undefined, c.d);
}

{
  this.c = 1;
  class C {
    static c = this.c;
  }

  assertEquals(undefined, C.c);

  let c = new C;
  assertEquals(undefined, c.c);
}

{
  class C {
    static c = 1;
    static d = this.c;
  }

  assertEquals(1, C.c);
  assertEquals(1, C.d);

  let c = new C;
  assertEquals(undefined, c.c);
  assertEquals(undefined, c.d);
}

{
  class C {
    static b = 1;
    static c = () => this.b;
  }

  assertEquals(1, C.b);
  assertEquals(1, C.c());

  let c = new C;
  assertEquals(undefined, c.c);
}

{
  let x = 'a';
  class C {
    static b = 1;
    static c = () => this.b;
    static e = () => x;
  }

  assertEquals(1, C.b);
  assertEquals('a', C.e());

  let a = {b : 2 };
  assertEquals(1, C.c.call(a));

  let c = new C;
  assertEquals(undefined, c.b);
  assertEquals(undefined, c.c);
}

{
  let x = 'a';
  class C {
    static c = 1;
    static d = function() { return this.c; };
    static e = function() { return x; };
  }

  assertEquals(1, C.c);
  assertEquals(1, C.d());
  assertEquals('a', C.e());

  C.c = 2;
  assertEquals(2, C.d());

  let a = {c : 3 };
  assertEquals(3, C.d.call(a));

  assertThrows(C.d.bind(undefined));

  let c = new C;
  assertEquals(undefined, c.c);
  assertEquals(undefined, c.d);
  assertEquals(undefined, c.e);
}

{
  class C {
    static c = function() { return 1 };
  }

  assertEquals('c', C.c.name);
}

{
  d = function() { return new.target; }
  class C {
    static c = d;
  }

  assertEquals(undefined, C.c());
  assertEquals(new d, new C.c());
}

{
  class C {
    static c = () => new.target;
  }

  assertEquals(undefined, C.c());
}

{
   class C {
     static c = () => {
       let b;
       class A {
         constructor() {
           b = new.target;
         }
       };
       new A;
       assertEquals(A, b);
     }
  }

  C.c();
}

{
  class C {
    static c = new.target;
  }

  assertEquals(undefined, C.c);
}

{
  class B {
    static d = 1;
    static b = () => this.d;
  }

  class C extends B {
    static c = super.d;
    static d = () => super.d;
    static e = () => super.b();
  }

  assertEquals(1, C.c);
  assertEquals(1, C.d());
  assertEquals(1, C.e());
}

{
  let foo = undefined;
  class B {
    static set d(x) {
      foo = x;
    }
  }

  class C extends B {
    static d = 2;
  }

  assertEquals(undefined, foo);
  assertEquals(2, C.d);
}


{
  let C  = class {
    static c;
  };

  assertEquals("C", C.name);
}

{
  class C {
    static c = new C;
  }

  assertTrue(C.c instanceof C);
}

(function test() {
  function makeC() {
    var x = 1;

    return class {
      static a = () => () => x;
    }
  }

  let C = makeC();
  let f = C.a();
  assertEquals(1, f());
})()

{
  var c = "c";
  class C {
    static ["a"] = 1;
    static ["b"];
    static [c];
  }

  assertEquals(1, C.a);
  assertEquals(undefined, C.b);
  assertEquals(undefined, C.c);
}
