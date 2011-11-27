args <- commandArgs(TRUE)
file <- args[1]

print(file)

d <- read.csv(file, header=T)
sink(file="summary.txt", split=T)
summary(d)
sink()

con <- file("summary.txt")
open(con)
summary_text <- readLines(con)
close(con)

png(sprintf("%s.png", file))

hist(d$EVAL,
     xlab="Eval")

for (i in 1:length(summary_text)) {
  str <- summary_text[[i]]
  mtext(str, line=(-2-i),
        cex=.8,
        family="Monospace")
}

dev.off()
q()


