args <- commandArgs(TRUE)
file <- args[1]
player <- args[2]

print(file)

d <- read.csv(file, header=T)

if (player == "white") {
  d$EVAL <- d$EVAL * -1
  player <- "white with eval reversed"
}

sink(file="summary.txt", split=T)
summary(d)
sink()

con <- file("summary.txt")
open(con)
summary_text <- readLines(con)
close(con)

png(sprintf("%s.png", file))

library(MASS)
truehist(d$EVAL,
         main=player,
         xlab="Eval",
         ylab="Frequency",
         prob=FALSE,
         h=50)

for (i in 1:length(summary_text)) {
  str <- summary_text[[i]]
  mtext(str,
        line=(-2-i),
        cex=.8,
        adj=0,
        family="Monospace")
}

dev.off()
q()


