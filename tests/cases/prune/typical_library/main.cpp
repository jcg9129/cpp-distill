// SPDX-License-Identifier: GPL-3.0-or-later
#pragma GCC optimize("O3")
#include <vector>
#include <numeric>
#include <utility>
#include <functional>
#if defined(__i386__) || defined(__x86_64__)
#pragma GCC target("avx2")
#endif
using namespace std;
#define rep(i, n) for (int i = 0; i < int(n); ++i)
struct Mint {
  static constexpr int mod = 998244353;
  int v;
  Mint(int x = 0) : v(x % mod) {}
  Mint& operator+=(Mint b) { v = (v + b.v) % mod; return *this; }
  [[nodiscard]] int value() const { return v; }
  friend Mint operator+(Mint a, Mint b) { return a += b; }
  friend Mint operator*(Mint a, Mint b) { return Mint(int(1LL * a.v * b.v % mod)); }
};
struct Sum {
  using S = Mint;
  static S op(S a, S b) { return a + b; }
  static S e() { return 0; }
  static S apply(S x, S tag, int len) { return x + tag * Mint(len); }
};
template<class M> struct LazySegTree {
  using S = typename M::S;
  int n;
  vector<S> tree, lazy;
  explicit LazySegTree(int size) : n(size), tree(4*size, M::e()), lazy(4*size, M::e()) {}
  void apply(int p, int l, int r, S tag) {
    tree[p] = M::apply(tree[p], tag, r-l);
    lazy[p] = M::op(lazy[p], tag);
  }
  void push(int p, int l, int r) {
    int m = (l+r)/2;
    apply(2*p, l, m, lazy[p]); apply(2*p+1, m, r, lazy[p]); lazy[p] = M::e();
  }
  void add(int a, int b, S tag, int p, int l, int r) {
    if (b <= l || r <= a) return;
    if (a <= l && r <= b) { apply(p,l,r,tag); return; }
    push(p,l,r); int m=(l+r)/2;
    add(a,b,tag,2*p,l,m); add(a,b,tag,2*p+1,m,r);
    tree[p] = M::op(tree[2*p],tree[2*p+1]);
  }
  S query(int a, int b, int p, int l, int r) {
    if (b <= l || r <= a) return M::e();
    if (a <= l && r <= b) return tree[p];
    push(p,l,r); int m=(l+r)/2;
    return M::op(query(a,b,2*p,l,m),query(a,b,2*p+1,m,r));
  }
};
template<class T> struct Fenwick {
  vector<T> bit;
  explicit Fenwick(int n) : bit(n+1) {}
  void add(int i, T x) { for (++i; i < int(bit.size()); i += i&-i) bit[i] += x; }
  T sum(int i) const { T x{}; for (; i; i -= i&-i) x += bit[i]; return x; }
};
struct DSU {
  vector<int> p;
  explicit DSU(int n) : p(n,-1) {}
  int find(int a) { return p[a]<0 ? a : p[a]=find(p[a]); }
  void unite(int a, int b) {
    a=find(a); b=find(b); if (a==b) return;
    if (p[a]>p[b]) swap(a,b);
    p[a]+=p[b]; p[b]=a;
  }
};
template<class T> struct SparseTable {
  vector<vector<T>> table;
  explicit SparseTable(vector<T> v) { table.push_back(v); }
  T query(int i) const { return table[0][i]; }
};
template<class... T> int total(T... x) { if constexpr (sizeof...(T)) return (x + ...); else return 0; }
__attribute__((always_inline)) inline int twice(int x) { return 2*x; }
int main() {
  LazySegTree<Sum> seg(8); seg.add(0,8,Mint(1),1,0,8);
  Fenwick<Mint> fen(8); rep(i,8) fen.add(i,Mint(i));
  DSU dsu(8); dsu.unite(0,1);
  auto [a,b] = pair<int,int>{1,2};
  auto recursive = [](auto self, int n) -> int { return n ? n+self(self,n-1) : 0; };
  function<int(int)> f = [a,&b](int x) { return a+b+x; };
  alignas(16) int result = total(seg.query(0,8,1,0,8).value(),fen.sum(8).value(),dsu.find(1));
  [[maybe_unused]] int extra = recursive(recursive,3);
  if (result) [[likely]] return f(twice(result));
  return 0;
}
