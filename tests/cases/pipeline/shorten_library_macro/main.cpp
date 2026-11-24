// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#define LOCAL_MACRO(parameterSpelling) (parameterSpelling + longCounter)
#define STRINGIZE(argumentSpelling) #argumentSpelling
#define FORWARD(argumentSpelling) STRINGIZE(argumentSpelling)
#define OBJECT_NAME longExpandedIdentifier
struct LongContainer {
 using value_type=int;
 int size(){return 4;}
 int* begin(){return &longStorage;}
 int* end(){return &longStorage+1;}
 int longStorage=8;
};
extern "C" int longCFunction(int value){return value+1;}
constexpr unsigned long long operator"" _longSuffix(unsigned long long value){return value;}
int main(){
 int x=2;
 int longCounter=3; auto parameterSpelling=1;
 auto longText=FORWARD(longArgument +  longArgument);
 auto longExpandedText=FORWARD(OBJECT_NAME);
 int longExpandedIdentifier=1;
 LongContainer longObject;
 LongContainer::value_type longResult=longObject.size()+*longObject.begin();
 std::printf("%d %s %llu %d\n",LOCAL_MACRO(parameterSpelling)+longResult,longText,2_longSuffix,longCFunction(x)+longExpandedIdentifier+int(longExpandedText[0] != 0));
}
