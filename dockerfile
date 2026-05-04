# PRE-REQUISITE
# get the indexed docs in the ./index folder from the google drive 
# link provided in README, otherwise the last step would result in
# an error.


# get gcc shite
FROM gcc:latest

# setup deps
RUN apt-get update
RUN apt-get install -y cmake

# setup working dir
WORKDIR /
COPY . .

# compile
RUN cmake -S . -B build
RUN cmake --build build

# run the exe
CMD ["./build/api_server ./index 8080"]

