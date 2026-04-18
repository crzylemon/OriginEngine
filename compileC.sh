# Use: ./compileC.sh <input.c> <output>
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <input.c> <output>"
    exit 1
fi
gcc $1 -o $2