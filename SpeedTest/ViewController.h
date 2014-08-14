//
//  ViewController.h
//  SpeedTest
//
//  Created by jason on 14-8-13.
//  Copyright (c) 2014年 chenyang. All rights reserved.
//

#import <UIKit/UIKit.h>

@interface ViewController : UIViewController
@property (weak, nonatomic) IBOutlet UITextField *addressTextField;
@property (weak, nonatomic) IBOutlet UILabel *resultLabel;
- (IBAction)buttonAction:(id)sender;

@end
