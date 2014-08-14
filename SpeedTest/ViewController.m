//
//  ViewController.m
//  SpeedTest
//
//  Created by jason on 14-8-13.
//  Copyright (c) 2014年 chenyang. All rights reserved.
//

#import "ViewController.h"
#include "dns_best_ip.h"

@interface ViewController () {
    BOOL isTesting_;
    NSString * fastIP_;
    NSString * address_;
}

@end

@implementation ViewController

- (void)viewDidLoad
{
    [super viewDidLoad];
	// Do any additional setup after loading the view, typically from a nib.
    isTesting_ = NO;
}

- (void)finishTest
{
    isTesting_ = NO;
    self.resultLabel.text = fastIP_;
}

- (void)test
{
    char * fast_ip = get_preferred_ip([address_ cStringUsingEncoding:NSUTF8StringEncoding]);
    if (fast_ip != NULL) {
        printf("\n\nfast ip : %s \n\n", fast_ip);
        fastIP_ = [NSString stringWithUTF8String:fast_ip];
        
        free(fast_ip);
    } else {
        fastIP_ = nil;
    }
    
    
    
    [self performSelectorOnMainThread:@selector(finishTest) withObject:nil waitUntilDone:NO];
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

- (IBAction)buttonAction:(id)sender {
    if (isTesting_) {
        return;
    }
    
    address_ = [self.addressTextField text];
    isTesting_ = YES;
    self.resultLabel.text = @"测速中....";
    
    [self performSelectorInBackground:@selector(test) withObject:nil];
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event
{
    if ([self.addressTextField isFirstResponder]) {
        [self.addressTextField resignFirstResponder];
    }
}

@end
