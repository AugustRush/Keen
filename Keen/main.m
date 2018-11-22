//
//  main.m
//  Keen
//
//  Created by pingwei liu on 2018/11/21.
//  Copyright © 2018 pingwei liu. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "Keen.h"


int main(int argc, const char * argv[]) {
    @autoreleasepool {
        // insert code here...
//        int8_t *raws = calloc(8, sizeof(int8_t));
//        raws[0] = 0xFF;
//        raws[1] = 0xFF;
//        raws[2] = 0xFF;
//        raws[3] = 0xEF;
//        int32_t *one = (int32_t *)raws;
//        printf("onr is %d\n",*one);
        
        KeenRef keen = KeenRefNew();
        
        int32_t value = 10000;
        
        KeenRefAddItem(keen, "some new key", &value,4,2);
        KeenRefAddItem(keen, "some new key1", &value,4,2);
        KeenRefAddItem(keen, "some new key2", &value,4,2);
        KeenRefAddItem(keen, "some new key3", &value,4,2);
        KeenRefAddItem(keen, "test1", "10",4,1);
        KeenRefAddItem(keen, "test2", "11",4,1);
        
        const BYTE *buffer = KeenRefGetItem(keen, "test1");
        printf("%s\n",buffer);
        
        const BYTE *bytes = KeenRefGetItem(keen, "some new key");
        int32_t *number = (int32_t *)(bytes + 1);
        int32_t number1;
        memcpy(&number1, bytes + 1, 4);
        printf("%d %d",bytes[0],*number,number1);
        
    }
    return 0;
}
