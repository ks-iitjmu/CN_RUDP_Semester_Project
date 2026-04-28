# ============================================================
# Makefile — RUDP Protocol Stack
# Group 3: Dhruv, Krishiv, Piyush, Vivek, Kunal
# ============================================================

CC      = gcc
CPP     = g++
CFLAGS  = -Wall -Wextra -O2 -std=c11 -pthread -I.
CPPFLAGS = -Wall -O2 -std=c++17 -pthread -I.
LDFLAGS = -pthread -lm -lX11
RAYLIB_FLAGS = -lraylib

# ─── Source Groups ──────────────────────────────────────────
RUDP_SRC  = rudp/rudp.c rudp/rudp_buffer.c rudp/rudp_timer.c rudp/rudp_nat.c
TRANS_SRC = transport/transport.c

RUDP_OBJ  = $(RUDP_SRC:.c=.o)
TRANS_OBJ = $(TRANS_SRC:.c=.o)

# ─── Binary Targets ─────────────────────────────────────────
BENCH_SENDER  = bench_sender
BENCH_RECV    = bench_receiver
RENDEZVOUS    = rudp_rendezvous
NAT_PUNCH     = rudp_nat_punch
RUDP_TEST     = rudp_test
CLI_SERVER    = cli_game_demo/game_server
CLI_CLIENT    = cli_game_demo/game_client
RAY_GAME      = game.out

ALL_BINS = $(BENCH_SENDER) $(BENCH_RECV) $(RENDEZVOUS) $(NAT_PUNCH) $(RUDP_TEST) $(CLI_SERVER) $(CLI_CLIENT) $(RAY_GAME)

# ─── Default ────────────────────────────────────────────────
.PHONY: all clean benchmark rendezvous nat_punch test cli_game run_game help

all: $(ALL_BINS)
	@echo ""
	@echo "╔═══════════════════════════════════════════╗"
	@echo "║  RUDP Build Complete — tools are ready    ║"
	@echo "╚═══════════════════════════════════════════╝"
	@echo ""
	@echo "  Benchmark:  ./bench_sender | ./bench_receiver"
	@echo "  Rendezvous: ./rudp_rendezvous"
	@echo "  NAT Punch:  ./rudp_nat_punch"
	@echo "  Unit test:  ./rudp_test"
	@echo "  Raylib Game: ./game.out (Run with 'make run_game')"
	@echo ""

benchmark: $(BENCH_SENDER) $(BENCH_RECV)
rendezvous: $(RENDEZVOUS)
nat_punch: $(NAT_PUNCH)
test:      $(RUDP_TEST)
cli_game:  $(CLI_SERVER) $(CLI_CLIENT)

# ─── Raylib Game Target ─────────────────────────────────────
$(RAY_GAME): game.cpp $(RUDP_OBJ) $(TRANS_OBJ)
	$(CPP) $(CPPFLAGS) $^ -o $@ $(RAYLIB_FLAGS) $(LDFLAGS)
	@echo "  [OK] $(RAY_GAME)"

run_game: $(RAY_GAME)
	export LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):/usr/local/lib; ./$(RAY_GAME)

# ─── RUDP Library (object files) ────────────────────────────
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ─── CLI Game Binaries ──────────────────────────────────────
$(CLI_SERVER): cli_game_demo/game_server.c $(RUDP_OBJ) $(TRANS_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "  [OK] $(CLI_SERVER)"

$(CLI_CLIENT): cli_game_demo/game_client.c $(RUDP_OBJ) $(TRANS_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) -lm
	@echo "  [OK] $(CLI_CLIENT)"

# ─── Benchmark Binaries ─────────────────────────────────────
$(BENCH_SENDER): benchmark/bench_sender.c $(RUDP_OBJ) $(TRANS_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "  [OK] $(BENCH_SENDER)"

$(BENCH_RECV): benchmark/bench_receiver.c $(RUDP_OBJ) $(TRANS_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "  [OK] $(BENCH_RECV)"

# ─── Rendezvous Server (standalone NAT helper) ──────────────
REND_SRC = tools/rudp_rendezvous_main.c
$(RENDEZVOUS): $(REND_SRC) $(RUDP_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "  [OK] $(RENDEZVOUS)"

# ─── NAT Hole Punching Tool ─────────────────────────────────
NAT_PUNCH_SRC = tools/rudp_nat_punch_main.c
$(NAT_PUNCH): $(NAT_PUNCH_SRC) $(RUDP_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "  [OK] $(NAT_PUNCH)"

# ─── RUDP Unit Test ─────────────────────────────────────────
TEST_SRC = tests/rudp_test.c
$(RUDP_TEST): $(TEST_SRC) $(RUDP_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "  [OK] $(RUDP_TEST)"

# ─── Clean ──────────────────────────────────────────────────
clean:
	rm -f $(ALL_BINS) $(RUDP_OBJ) $(TRANS_OBJ)
	rm -f sender.log receiver.log report.csv
	@echo "  [Clean] done."

# ─── Help ───────────────────────────────────────────────────
help:
	@echo "Targets:"
	@echo "  make all        — Build everything"
	@echo "  make run_game   — Build and run the Raylib game"
	@echo "  make cli_game   — game_server + game_client (CLI)"
	@echo "  make clean      — Remove binaries and logs"
