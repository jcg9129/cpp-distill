// SPDX-License-Identifier: GPL-3.0-or-later
struct LiveFriend { template<class T> friend int friendCall(T); };
template<class T> int friendCall(T){return 3;}
struct DeadFriend { template<class T> friend int deadFriend(T); };
template<class T> int deadFriend(T){return 7;}
int main(){LiveFriend x; (void)x; return friendCall(0)-3;}
