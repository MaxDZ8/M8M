#M8M
![Icons](https://github.com/MaxDZ8/M8M/blob/gh-pages/M8M-normal-states.gif)

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

**M8M has been written targeting AMD GCN hardware**: that's video cards series 7000 or Rx-200, including most APUs. If you don't know what I'm talking about you'll have to look for a Radeon™ sticker on your PC.

M8M is a miner written to spread awareness of what hashing is and how it relates to crypto. Comprension.

Since I mine crypto while working, **M8M is geared toward people working on computers equipped with GPUs that happen to be (mostly) idle** or with plenty of unused power. So we are willing to pay some extra electricity because it's so sad to leave them underutilized for most of the time: our computers are already on!

M8M is also geared towards **people who find current miners too difficult**. The interface is still a bit clunky but at least you won't have to read endless, often incomplete and contradictory documentation to figure it out. We have other things to do!

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
Cryptocurrency mining is an intensive operation for your computer. If you don't know what this means, then pay special attention here.

The following are all things that **might happen** to your computer while using M8M (as well as any others performance-intensive applications):
* it can hang due to *this* or *that* component getting too hot;
* it can hang and refuse to turn on again for a while;
* it can hang and refuse to turn on ever again!
* its lifespan might be reduced;
* its stability might be compromised;
* you might lose half an hour of work... 
* ... you might lose all your work in some circumstances!
* in some extreme circumstances electrical faults and fires might happen.

Be warned that M8M and cryptocurrency miners in general will rise your electricity bill.

They will also generate a fairly typical (but small) network traffic which might expose you to unwanted attention (if you want to mine at work, check out your company allows that).

M8M uses the network for configuration. This **must be considered a security risk. It your responsability to secure your network and computer**, M8M assumes to be run in a "demilitarized zone".

If you don't know what this means or you cannot assess how the above affects you please do not use M8M without obtaining and understanding proper information.

No matter what happens to you, I will take no responsability. **You use M8M at your own risk**.

###A guarantee

***I have written M8M with no malicious intent.***

In other words, it is **not** my intention to damage you, your computer or your data. It is **not** my intention to somehow obtain an unfair advantage by exploiting your resources.

##Usage

This document does not include installation nor usage instructions as it focuses on source code.
See github pages instead: http://maxdz8.github.io/M8M/

##Building 

Compared to legacy miners the list of dependancies is limited you'll need:
- Windows Platform SDK for desktop apps.
- Some OpenCL SDK;
- Optionally, [RapidJSON includes](https://github.com/miloyip/rapidjson) already provided here for convenience.

There are no `define`s to be used in production builds.
Link dependancies include:
    OpenCL.lib;Ws2_32.lib;Gdiplus.lib;Shell32.lib
If you want to update rapidjson either overwrite the existing files in the local-include directory or set up proper include paths.

As M8M currently targets Windows®, you'll hopefully be just fine downloading the Visual Studio 2012 solution file, as soon as it is released.
