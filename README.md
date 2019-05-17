# Soybean-Crush-Spread-Trading-Algorithm

This strategy was created during a Financial Market research course at Carthage College during the spring 2018 semester. The course was led by professor Alex Lau and we worked with RCM Alternatives from Chicago to develop an implementation of a mean reversion trading algorithm for the soy bean crush spread. There were three other students in the course with majors in finance and accounting and I used my knowledge of trading and computer science to put the theory into code.

RCM Alternatives created a C++ library to make trading strategies. After compiling the strategy, the created .dll file is used in the Strategy Studio application. By feeding Strategy Studio past tick data, the user's algorithm can be tested and modified after seeing its performance.

The C++ file names do not match the actual trading strategy implemented in the code due to Visual Studio not agreeing with my attempts to change all instances where the old name was used. The algorithm used in this project was a mean reversion strategy and based off the findings by John Mitchell from Central Michigan University. A more in depth explanation of the project can be found in the PowerPoint my team presented.
