#!/usr/bin/env python3
'''
    根据chrome浏览器2017年发布的新特性,
    需要unix版本的chrome版本高于57,
    windows版本的chrome版本高于58,
    才能使用无界面运行.
'''
from selenium import webdriver
from selenium.webdriver.chrome.options import Options
import time


chrome_opt = Options() #创建参数设置对象.
#chrome_opt.add_argument('--headless') #无界面化.
#chrome_opt.add_argument('--disable-gpu') #配合上面的无界面化.
chrome_opt.add_argument('--start-maximized') #设置窗口最大化.
chrome_opt.add_argument('--disable-infobars') #禁用浏览器正在被自动化程序控制的提示.


#Chrome禁用图片加载参数设置.
'''
prefs = {
    'profile.default_content_setting_values' : {
        'images' : 2
    }
}
chrome_opt.add_experimental_option('prefs',prefs)
'''

#Chrome禁止弹窗参数设置.
'''
prefs = {  
    'profile.default_content_setting_values' :  {  
        'notifications' : 2  
     }  
}  
chrome_opt.add_experimental_option('prefs',prefs) 
'''


#创建Chrome对象并传入设置信息.
#driver = webdriver.Chrome(executable_path='chromedriver.exe') #可以自己传入chromedriver.exe的路径.
driver = webdriver.Chrome(chrome_options=chrome_opt)

'''
Driver对象常见操作
get(url): 在当前浏览器会话中访问传入的url地址, driver.get('https://www.baidu.com').
close(): 关闭浏览器当前窗口。
quit(): 退出webdriver并关闭所有窗口。
refresh(): 刷新当前页面。
title: 获取当前页的标题。
page_source: 获取当前页渲染后的源代码。
current_url: 获取当前页面的url。
window_handles: 获取当前会话中所有窗口的句柄。
'''

#操作这个对象.
driver.get('https://www.baidu.com') #get方式访问百度.
time.sleep(5)
driver.get('https://github.com/zhixianggao/WorkStation/tree/develop') #get方式访问my github.
time.sleep(10)
print(driver.page_source) #print page code to prove program is right.
driver.refresh() #刷新当前页面.

driver.quit() #使用完, 记得关闭浏览器, 不然chromedriver.exe进程为一直在内存中.













