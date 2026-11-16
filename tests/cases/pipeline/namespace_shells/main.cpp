// SPDX-License-Identifier: GPL-3.0-or-later
namespace removed { namespace nested { int dead; } }
namespace removed { int alsoDead; }
inline namespace removedInline { int deadInline; }
namespace { namespace { int deadAnonymous; } }
namespace removedOuter::removedInner { int deadNested; }
namespace used { int x(){return 3;} }
namespace used { int deadUsed; }
namespace viaUsing { int deadUsing; }
using namespace viaUsing;
namespace viaAlias { int deadAlias; }
namespace alias = viaAlias;
using namespace alias;
namespace outside { namespace inside {} }
using namespace outside::inside;
int main(){return used::x()-3;}
