#M8M
*An (hopefully) educational cryptocurrency miner.*

--------------------------------------------------------------------------------

##Foreword

I was introduced to cryptocurrencies at the end of 2013 by a friend of mine. I immediately recognized the concept as very interesting but my interest was limited to a better understanding of the most simple mechanics involving hardware and computers. Since my first mining attempts I was somewhat pissed off by the approach used by standard mining software. For something that was GPU-driven, they were surprisingly *oldschool* with terminal interfaces and ncurses.
By spending more time on my mining operations and surfing forums, I noticed there was very little understanding of what a GPU is and how it works. This was very surprising! After all GPUs are nothing new, having powered games for decades and even high-performance computing for quite a few years.

Due to life issues, I also found my skills at GPU programming in a serious need for updating, being currently back at GL2, D3D9 and some GPGPU techniques on top of the crazy Win32, C/C++, performance computing and hardware stuff.

Put those two things together and you'll understand why I found out writing a miner was possibly the best thing **to pull me up to date to OpenCL and more modern GPU architectures**.
There has been a lot of learning, even though miners are not typical GPU-accelerated applications.
Always from a learning perspective, I tried to keep the code readable and maintainable rather than efficient. I would like you to say the code is nice, readable and explicative but it is a bit ugly here and there and there are a lot of todos and quite a few bugs.

In terms of effort, most of the time has been spent on the kernels. People seasoned in cryptography tend to produce unclear code, with variable names such as *p*, *W*, *k* (which you'd expect to be *constant*, to later discover it's a *variable key* instead), sometimes they add some very math-heavy documentation.
Other people involved in cryptocurrencies worked on GPU kernels by basically porting CPU code to OpenCL.
M8M has not taken any code from any miner and maybe a few lines of code from sphlib. In general I read the code, understood it and made my own, hopefully more GPU-oriented version which has demostrated to be mathematically equivalent. Digest algorithms don't allow many differences so of course the resulting code is still similar. You'll have to take my word for it.
Credit is due where credit is deserved however so I'll try to remember who or what helped M8M in seeing the light:

* First of all, whoever helped, assisted or just had a chat with me about my hobby GPU, C and games programming. Those were indeed the most important contributors motivating me.
* AMD, for making affordable computing equipment real. I'd probably would have never got into computing if it wasn't for them. Try to not go belly up!
* Satoshi Nakamoto for BitCoin.
* My friend AleParme for telling me someone invented those this odd thing.
* Con Kolivas for what was probably the first OpenCL scrypt kernel which I used as a start. It seems he also did a lot of other things but I don't think I care.
* Arebyp. This one is odd. Basically, when I started looking at scrypt (about January 2014) I had two ideas: either parallelize it or try to turn the scrypt loops "inside out". My idea didn't quite worked as expected (albeit I've learned a lot). Arebyp apparently did the other thing and so he saved me quite some work!
* The folks involved in MyriadCoin development - I still haven't understood who I should thank for what. In the end what it matters is that they had me take a look at this chained hashing thing by inventing a coin that I feel like holding and expanding the cryptocurrency technology to a mindset change. Hopefully it will be the coin that we'll end up using everyday!
* Project Saphir for SPHlib and Thomas Pornin for being a go-to man in the field.
* PrettyHateMachine for sgminer-sph. Nice work here with SIMD_Q.

A special "thank you" to the people spending their spare time with me. 

So, I told you about how M8M was born, how it was developed and who did what.
Now the question is: **what is M8M? What is the result?**

**M8M runs on the Microsoft® Windows® operating system. You'll need at least version 7.**

**M8M has been written targeting AMD GCN hardware**: that's video cards series 7**nnn** or R**n**-2**nn**, including APUs. If you don't know what I'm talking about you'll have to look for a Radeon™ sticker on your PC.

M8M is a miner written to spread awareness of what hashing is and how it relates to crypto. Comprension.

Since I mine crypto while working, **M8M is geared toward people working on computers equipped with GPUs that happen to be idle**. So we want to pay some extra electricity because it's so sad to leave them unused for most of the time and because our computers are already on.

M8M is also geared towards **people who find current miners too difficult**. The interface is still a bit clunky but at least you won't have to read endless documentation and figure it out. We have other things to do!

In my tests, M8M also turned out to be faster than standard miners. Perhaps it will be faster for you too.

Keep in mind M8M is very minimalistic. Other miners are indeed more complete and featured.

###A note on the name

What does the name mean? I haven't decided yet.
By sure, the only wrong way to pronounce it is spelling the letters. It sounds ugly.
The following are all ideas I've been considering:
- "mate 'em": because M8M has been doing multiphased hashing since day one;
- MaxDZ8's Miner: I find a bit boring to just add my initials to program executable;
- "my team": the "team" is built from the various ALUs in a GPU. I currently use the term to refer to a "local-x" slice of a workgroup, don't ask;
- "M8y miner": 'cuz I totally need some self-celebration :P

##On safety

###A warning
Cryptocurrency mining is an intensive operation for your computer. If you don't know what this means, I suggest you to not proceed further. Thank you for reading so far!

The following are all things that **might happen** to your computer while using M8M:
* it can hang due to this or that component getting too hot;
* it can hang and refuse to turn on again for a while;
* it can hang and refuse to turn on ever again!
* its lifespan might be reduced;
* its stability might be compromised;
* you might lose half an hour of work... 
* ... you might lose all your work in some circumstances!
* in some extreme circumstances electrical faults and fires might happen.

Be warned that M8M and cryptocurrency miners in general will rise your electricity bill.

They will also generate a fairly typical (but small) network traffic which might expose you to unwanted attention (if you want to mine at work, check out your company allows that).

M8M uses the network for configuration. This must be considered a security risk. It your responsability to secure your network and computer, M8M assumes to be run in a "militarized zone".

If you don't know what this means or you cannot assess how the above affects you please do not use M8M without obtaining and understanding proper information.

###A guarantee

***I have written M8M with no malicious intent.***

In other words, it is ***not*** my intention to damage you, your computer or your data. It is ***not*** my intention to somehow obtain an unfair advantage by exploiting your resources.

##Usage

###Installation
**You're probably better off with the installer**. If you want to enable web monitoring or administration you'll need administration rights on your machine as this needs to setup a couple of firewall rules.

The standalone executable can be run without administrative rights but you'll need to set up the environment yourself. Configuring M8M will also require some work.

###First run
Differently from "legacy" miners, M8M requires a graphical user interface. If this does not make any sense to you, then you probably don't need to bother.

Run M8M by clicking on its icon or by using your OS launcher. In the latter case, hitting the button between CTRL and ALT on your keyboard, writing "M8M" and hitting ENTER will be sufficient.
A small icon and a popup should show up in your taskbar notification area. It will be a warning icon asking you to configure your miner.

Right-click on M8M icon, a popup menu will appear. Click on "Enable web administration".
Right-click on M8M icon again. Click on "Connect to web administration".

Your browser will open. Following the wizard should be sufficient.

> Note: the suggested browser is Mozilla Firefox.

> Note: always remember to save settings before closing the browser window.

###Successive runs / production
M8M will run from the saved settings: you don't need to bother.

Unless told to do otherwise, M8M will load its settings from

    %APPDATA%\M8M\init.json

> Note: if something is already there, I'm sorry to inform you M8M might have overwritten your data. Let me know if this happened.

###Advanced usage: multiple configurations
If you want to load a different configuration file, create a shortcut or a batch file running M8M using the `--config` parameter. It could be something like:

    M8M.exe --config=path_to_different_config.json
	
This is the only supported command line parameter. Note M8M will always load `init.json` by default and you cannot change this. Even if you save different configs, M8M will not use them at load by default.

##Building 

Compared to legacy miners the list of dependanies is limited you'll need:
- Windows Platform SDK for desktop apps.
- Some OpenCL SDK;
- Some version of [gflags](https://github.com/schuhschuh/gflags) (likely to be phased out);
- Some version of [jsoncpp](http://sourceforge.net/projects/jsoncpp) (likely to be replaced);

There are no `define`s to be used in production builds.
Link dependancies include:
    OpenCL.lib;Ws2_32.lib;Gdiplus.lib;Shell32.lib
Of course you'll need to link gflags and jsoncpp as well.

As M8M currently targets Windows®, you'll hopefully be just fine downloading the Visual Studio 2012 solution file.

##Algorithms supported
###Qubit
###Scrypt n=1024
Not to be used. This was my first attempt at modern OpenCL and it didn't quite work out.
