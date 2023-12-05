# shared-state-server
Test task for implementing multithreaded Shared-State server using asio

# Update Nov 30, 2023
The company refused me because of bugs... It's OK, bugs can be everywhere, because programmers are people too =)

I'm curious and trying to understand what these bugs are...

Another surprising thing:
- It turns out that this test was NOT for proficiency in ASIO, but for the ability to write without bugs?
- Or was it a test of tester skills?
- Or was it a test of the desire to not only write a server for free and spend 9 hours on it, but also spend time on full testing for free? Moreover, in the accompanying note I indicated that there may be bugs because full testing was not carried out!
- Or did the company need someone to implement this task for free?

This all looks very strange...

# Update Dec 1, 2023
Guys, please stop writing me tons of emails and questions, I don’t want to spend all day answering!
I'll add more information in the README.

# Update Dec 4, 2023
Answers to some questions:
- It was the AJAX company: https://ajax.systems/
- I fixed two bugs: 1) simultaneous access to a resource from several threads, 2) using `std::string` here was a mistake (I've never done this before, I usually used `vector<char>`). The thing is that `std::string` due to the presence of Small String Optimisation does not behave as you would expect, and instead of a real move of `std::string` object - copying occurs, which leads to invalidation of pointers to an internal char array. Now everything works as it should. Later I will add some optimizations for memory reuse and so on...

# Update Dec 5, 2023
The code was partially refactored.
